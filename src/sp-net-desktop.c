// posix sockets backend for sp-net.h; lan mode only, ad-hoc is vita-only
#include "sp-net.h"

#ifndef __vita__

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define SPNET_DISCOVERY_PORT    19741
#define SPNET_TCP_PORT          19742
#define SPNET_QUERY             "RSCV1?"
#define SPNET_QUERY_LEN         6
#define SPNET_REPLY_PREFIX      "RSCV1!"
#define SPNET_REPLY_PREFIX_LEN  6
#define SPNET_SCAN_WINDOW_MS    1500
#define SPNET_SCAN_RESEND_MS    300
#define SPNET_MAX_SCAN_RESULTS  32
#define SPNET_CONN_TABLE_SIZE   (SPNET_MAX_GUESTS + 4)

typedef struct {
    int used;
    int fd;
    int state; // 0 connecting, 1 established, -1 dead
    int is_guest_of_host;
} spnet_conn_t;

// shared state, guarded by g_lock unless noted main-thread only
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static spnet_mode g_mode = SPNET_MODE_OFF; // main-thread only
static spnet_status g_status = SPNET_STATUS_OFF; // main-thread only
static char g_error_text[80] = {0}; // main-thread only

static int g_disc_sock = -1; // main-thread only (pump owns discovery)
static int g_listen_sock = -1; // main-thread only
static char g_host_name[64] = {0};
static int g_host_max_guests = 0;

static spnet_conn_t g_conns[SPNET_CONN_TABLE_SIZE]; // lock

static int g_accept_queue[SPNET_MAX_GUESTS]; // lock
static int g_accept_head = 0, g_accept_tail = 0, g_accept_count = 0;

static spnet_world_info g_scan_results[SPNET_MAX_SCAN_RESULTS]; // lock
static int g_scan_count = 0; // lock
static int g_scan_active = 0; // main-thread only
static uint64_t g_scan_t0 = 0;
static uint64_t g_scan_last_send = 0;

// small helpers

static uint64_t spnet_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static int spnet_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int spnet_parse_address(const char *address, struct sockaddr_in *out) {
    char ip[64];
    int port = 0;

    if (!address) return -1;
    if (sscanf(address, "%63[^:]:%d", ip, &port) != 2) return -1;
    if (port <= 0 || port > 65535) return -1;

    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, ip, &out->sin_addr) != 1) return -1;

    return 0;
}

// connection table; caller must hold g_lock for the _locked variants

static int conn_alloc_locked(void) {
    for (int i = 0; i < SPNET_CONN_TABLE_SIZE; i++) {
        if (!g_conns[i].used) {
            g_conns[i].used = 1;
            g_conns[i].fd = -1;
            g_conns[i].state = 0;
            g_conns[i].is_guest_of_host = 0;
            return i;
        }
    }
    return -1;
}

static spnet_conn_t *conn_get_locked(int idx) {
    if (idx < 0 || idx >= SPNET_CONN_TABLE_SIZE) return NULL;
    if (!g_conns[idx].used) return NULL;
    return &g_conns[idx];
}

static int conn_count_established_guests_locked(void) {
    int n = 0;
    for (int i = 0; i < SPNET_CONN_TABLE_SIZE; i++) {
        if (g_conns[i].used && g_conns[i].is_guest_of_host && g_conns[i].state == 1) {
            n++;
        }
    }
    return n;
}

// advances a still-connecting fd's state once non-blocking connect() has resolved
static void conn_poll_connect(int idx) {
    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(idx);
    int fd = -1;
    if (c && c->state == 0) fd = c->fd;
    pthread_mutex_unlock(&g_lock);

    if (fd < 0) return;

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);
    struct timeval tv = {0, 0};

    int r = select(fd + 1, NULL, &wfds, NULL, &tv);
    int new_state = 0;

    if (r > 0 && FD_ISSET(fd, &wfds)) {
        int err = 0;
        socklen_t elen = sizeof(err);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
        new_state = (err == 0) ? 1 : -1;
    } else if (r < 0) {
        new_state = -1;
    }

    if (new_state != 0) {
        pthread_mutex_lock(&g_lock);
        c = conn_get_locked(idx);
        if (c && c->state == 0) c->state = new_state;
        pthread_mutex_unlock(&g_lock);
    }
}

// discovery

static int spnet_parse_reply(const char *payload, char *name_out, size_t name_cap,
                             int *players_out, int *port_out) {
    const char *p1 = strchr(payload, '|');
    if (!p1) return -1;
    const char *p2 = strchr(p1 + 1, '|');
    if (!p2) return -1;

    size_t namelen = (size_t)(p1 - payload);
    if (namelen >= name_cap) namelen = name_cap - 1;
    memcpy(name_out, payload, namelen);
    name_out[namelen] = '\0';

    *players_out = atoi(p1 + 1);
    *port_out = atoi(p2 + 1);
    return 0;
}

static void spnet_record_scan_reply(const char *payload, const struct sockaddr_in *from) {
    char name[64];
    int players = -1;
    int port = SPNET_TCP_PORT;

    if (spnet_parse_reply(payload, name, sizeof(name), &players, &port) != 0) return;

    char addr[64];
    snprintf(addr, sizeof(addr), "%s:%d", inet_ntoa(from->sin_addr), port);

    pthread_mutex_lock(&g_lock);
    int idx = -1;
    for (int i = 0; i < g_scan_count; i++) {
        if (strcmp(g_scan_results[i].address, addr) == 0) {
            idx = i;
            break;
        }
    }
    if (idx < 0 && g_scan_count < SPNET_MAX_SCAN_RESULTS) {
        idx = g_scan_count++;
    }
    if (idx >= 0) {
        snprintf(g_scan_results[idx].name, sizeof(g_scan_results[idx].name), "%s", name);
        snprintf(g_scan_results[idx].address, sizeof(g_scan_results[idx].address), "%s", addr);
        g_scan_results[idx].player_count = players;
    }
    pthread_mutex_unlock(&g_lock);
}

// broadcasts the discovery query on every reachable target: lan broadcast, loopback broadcast, loopback unicast
static void spnet_send_query(void) {
    if (g_disc_sock < 0) return;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(SPNET_DISCOVERY_PORT);

    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(g_disc_sock, SPNET_QUERY, SPNET_QUERY_LEN, 0, (struct sockaddr *)&dst, sizeof(dst));

    dst.sin_addr.s_addr = inet_addr("127.255.255.255");
    sendto(g_disc_sock, SPNET_QUERY, SPNET_QUERY_LEN, 0, (struct sockaddr *)&dst, sizeof(dst));

    dst.sin_addr.s_addr = inet_addr("127.0.0.1");
    sendto(g_disc_sock, SPNET_QUERY, SPNET_QUERY_LEN, 0, (struct sockaddr *)&dst, sizeof(dst));
}

static void pump_discovery(void) {
    if (g_disc_sock < 0) return;

    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        char buf[256];

        int n = (int)recvfrom(g_disc_sock, buf, sizeof(buf) - 1, 0,
                              (struct sockaddr *)&from, &fl);
        if (n <= 0) break;
        buf[n] = '\0';

        if (g_status == SPNET_STATUS_HOSTING) {
            if (n == SPNET_QUERY_LEN && memcmp(buf, SPNET_QUERY, SPNET_QUERY_LEN) == 0) {
                pthread_mutex_lock(&g_lock);
                int guests = conn_count_established_guests_locked();
                char name[64];
                snprintf(name, sizeof(name), "%s", g_host_name);
                pthread_mutex_unlock(&g_lock);

                char reply[160];
                int rl = snprintf(reply, sizeof(reply), "%s%s|%d|%d",
                                  SPNET_REPLY_PREFIX, name, guests, SPNET_TCP_PORT);
                if (rl > 0) {
                    sendto(g_disc_sock, reply, (size_t)rl, 0, (struct sockaddr *)&from, fl);
                }
            }
        } else if (g_scan_active) {
            if (n > SPNET_REPLY_PREFIX_LEN &&
                memcmp(buf, SPNET_REPLY_PREFIX, SPNET_REPLY_PREFIX_LEN) == 0) {
                spnet_record_scan_reply(buf + SPNET_REPLY_PREFIX_LEN, &from);
            }
        }
    }
}

static void pump_accept(void) {
    if (g_listen_sock < 0) return;

    for (;;) {
        struct sockaddr_in from;
        socklen_t fl = sizeof(from);
        int fd = accept(g_listen_sock, (struct sockaddr *)&from, &fl);
        if (fd < 0) break;

        spnet_set_nonblocking(fd);
        int set = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &set, sizeof(set));

        pthread_mutex_lock(&g_lock);
        int idx = conn_alloc_locked();
        if (idx < 0) {
            pthread_mutex_unlock(&g_lock);
            close(fd);
            continue;
        }

        g_conns[idx].fd = fd;
        g_conns[idx].state = 1; // accept() means the handshake is already done
        g_conns[idx].is_guest_of_host = 1;

        if (g_accept_count < SPNET_MAX_GUESTS) {
            g_accept_queue[g_accept_tail] = idx;
            g_accept_tail = (g_accept_tail + 1) % SPNET_MAX_GUESTS;
            g_accept_count++;
        } else {
            // accept queue full: nobody will ever drain this slot
            close(fd);
            g_conns[idx].used = 0;
        }
        pthread_mutex_unlock(&g_lock);
    }
}

static void pump_scan_timeout(void) {
    if (!g_scan_active) return;

    uint64_t now = spnet_now_ms();
    if (now - g_scan_last_send >= SPNET_SCAN_RESEND_MS) {
        spnet_send_query();
        g_scan_last_send = now;
    }

    if (now - g_scan_t0 >= SPNET_SCAN_WINDOW_MS) {
        g_scan_active = 0;
        if (g_status == SPNET_STATUS_SCANNING) g_status = SPNET_STATUS_READY;
    }
}

static void pump_connecting(void) {
    for (int i = 0; i < SPNET_CONN_TABLE_SIZE; i++) {
        int need = 0;
        pthread_mutex_lock(&g_lock);
        if (g_conns[i].used && g_conns[i].state == 0) need = 1;
        pthread_mutex_unlock(&g_lock);
        if (need) conn_poll_connect(i);
    }
}

// public API (see sp-net.h for the contract)

int spnet_init(spnet_mode mode) {
    spnet_shutdown();

    g_error_text[0] = '\0';

    if (mode == SPNET_MODE_OFF) {
        g_mode = SPNET_MODE_OFF;
        g_status = SPNET_STATUS_OFF;
        return 0;
    }

    if (mode == SPNET_MODE_ADHOC) {
        g_mode = SPNET_MODE_ADHOC;
        g_status = SPNET_STATUS_ERROR;
        snprintf(g_error_text, sizeof(g_error_text), "Ad-hoc requires a Vita");
        return -1;
    }

    g_mode = SPNET_MODE_LAN;
    g_status = SPNET_STATUS_READY; // desktop: LAN connectivity is always available
    return 0;
}

void spnet_shutdown(void) {
    if (g_disc_sock >= 0) { close(g_disc_sock); g_disc_sock = -1; }
    if (g_listen_sock >= 0) { close(g_listen_sock); g_listen_sock = -1; }

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < SPNET_CONN_TABLE_SIZE; i++) {
        if (g_conns[i].used && g_conns[i].fd >= 0) close(g_conns[i].fd);
        g_conns[i].used = 0;
        g_conns[i].fd = -1;
        g_conns[i].state = -1;
        g_conns[i].is_guest_of_host = 0;
    }
    g_accept_head = g_accept_tail = g_accept_count = 0;
    g_scan_count = 0;
    pthread_mutex_unlock(&g_lock);

    g_scan_active = 0;
    g_host_name[0] = '\0';
    g_host_max_guests = 0;

    g_mode = SPNET_MODE_OFF;
    g_status = SPNET_STATUS_OFF;
}

spnet_mode spnet_get_mode(void) { return g_mode; }
spnet_status spnet_get_status(void) { return g_status; }

const char *spnet_status_text(void) {
    static char buf[80];

    switch (g_status) {
    case SPNET_STATUS_OFF: return "Not connected";
    case SPNET_STATUS_STARTING: return "Connecting...";
    case SPNET_STATUS_NO_NETWORK: return "Not connected";
    case SPNET_STATUS_READY: return "Connected";
    case SPNET_STATUS_SCANNING: return "Scanning...";
    case SPNET_STATUS_HOSTING: {
        pthread_mutex_lock(&g_lock);
        int n = conn_count_established_guests_locked();
        pthread_mutex_unlock(&g_lock);
        snprintf(buf, sizeof(buf), "Hosting (%d guest%s)", n, n == 1 ? "" : "s");
        return buf;
    }
    case SPNET_STATUS_ERROR:
        return g_error_text[0] ? g_error_text : "Error";
    }

    return "Not connected";
}

void spnet_pump(void) {
    if (g_mode != SPNET_MODE_LAN) return;

    pump_discovery();
    pump_accept();
    pump_scan_timeout();
    pump_connecting();
}

// no ad-hoc backend on desktop, so no vita netcheck dialog can be active
int spnet_dialog_active(void) { return 0; }

int spnet_scan_start(void) {
    if (g_mode != SPNET_MODE_LAN) return -1;

    if (g_disc_sock < 0) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) return -1;

        int bopt = 1;
        setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &bopt, sizeof(bopt));
        int reuse = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
        spnet_set_nonblocking(fd);

        struct sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        bind_addr.sin_port = 0;
        if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
            close(fd);
            return -1;
        }

        g_disc_sock = fd;
    }

    pthread_mutex_lock(&g_lock);
    g_scan_count = 0;
    pthread_mutex_unlock(&g_lock);

    g_scan_active = 1;
    g_scan_t0 = spnet_now_ms();
    spnet_send_query();
    g_scan_last_send = g_scan_t0;

    if (g_status != SPNET_STATUS_HOSTING) g_status = SPNET_STATUS_SCANNING;

    return 0;
}

int spnet_scan_results(spnet_world_info *out, int max) {
    if (!out || max <= 0) return 0;

    pthread_mutex_lock(&g_lock);
    int n = g_scan_count < max ? g_scan_count : max;
    for (int i = 0; i < n; i++) out[i] = g_scan_results[i];
    pthread_mutex_unlock(&g_lock);

    return n;
}

int spnet_host_start(const char *display_name, int max_guests) {
    if (g_mode != SPNET_MODE_LAN) return -1;

    if (g_disc_sock >= 0) { close(g_disc_sock); g_disc_sock = -1; }
    if (g_listen_sock >= 0) { close(g_listen_sock); g_listen_sock = -1; }

    int dfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (dfd < 0) return -1;

    int reuse = 1;
    setsockopt(dfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    spnet_set_nonblocking(dfd);

    struct sockaddr_in dbind;
    memset(&dbind, 0, sizeof(dbind));
    dbind.sin_family = AF_INET;
    dbind.sin_addr.s_addr = htonl(INADDR_ANY);
    dbind.sin_port = htons(SPNET_DISCOVERY_PORT);
    if (bind(dfd, (struct sockaddr *)&dbind, sizeof(dbind)) < 0) {
        close(dfd);
        return -1;
    }

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        close(dfd);
        return -1;
    }

    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    spnet_set_nonblocking(lfd);

    struct sockaddr_in lbind;
    memset(&lbind, 0, sizeof(lbind));
    lbind.sin_family = AF_INET;
    lbind.sin_addr.s_addr = htonl(INADDR_ANY);
    lbind.sin_port = htons(SPNET_TCP_PORT);
    if (bind(lfd, (struct sockaddr *)&lbind, sizeof(lbind)) < 0) {
        close(dfd);
        close(lfd);
        return -1;
    }

    int backlog = max_guests > 0 ? max_guests : SPNET_MAX_GUESTS;
    if (backlog > SPNET_MAX_GUESTS) backlog = SPNET_MAX_GUESTS;
    if (listen(lfd, backlog) < 0) {
        close(dfd);
        close(lfd);
        return -1;
    }

    g_disc_sock = dfd;
    g_listen_sock = lfd;
    g_host_max_guests = backlog;
    snprintf(g_host_name, sizeof(g_host_name), "%s",
            display_name ? display_name : "single-player world");

    g_status = SPNET_STATUS_HOSTING;
    return 0;
}

void spnet_host_stop(void) {
    // stops accepting new guests/advertising; established connections are left for the caller to close
    if (g_listen_sock >= 0) { close(g_listen_sock); g_listen_sock = -1; }
    if (g_disc_sock >= 0) { close(g_disc_sock); g_disc_sock = -1; }

    pthread_mutex_lock(&g_lock);
    g_accept_head = g_accept_tail = g_accept_count = 0;
    pthread_mutex_unlock(&g_lock);

    if (g_mode == SPNET_MODE_LAN) g_status = SPNET_STATUS_READY;
}

int spnet_host_accept(void) {
    pthread_mutex_lock(&g_lock);
    int idx = -1;
    if (g_accept_count > 0) {
        idx = g_accept_queue[g_accept_head];
        g_accept_head = (g_accept_head + 1) % SPNET_MAX_GUESTS;
        g_accept_count--;
    }
    pthread_mutex_unlock(&g_lock);
    return idx;
}

int spnet_connect(const char *address) {
    if (g_mode != SPNET_MODE_LAN) return -1;

    struct sockaddr_in addr;
    if (spnet_parse_address(address, &addr) != 0) return -1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    spnet_set_nonblocking(fd);
    int set = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &set, sizeof(set));

    pthread_mutex_lock(&g_lock);
    int idx = conn_alloc_locked();
    if (idx < 0) {
        pthread_mutex_unlock(&g_lock);
        close(fd);
        return -1;
    }
    g_conns[idx].fd = fd;
    g_conns[idx].is_guest_of_host = 0;
    pthread_mutex_unlock(&g_lock);

    int r = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    int state;
    if (r == 0) {
        state = 1;
    } else if (errno == EINPROGRESS) {
        state = 0;
    } else {
        state = -1;
    }

    pthread_mutex_lock(&g_lock);
    g_conns[idx].state = state;
    pthread_mutex_unlock(&g_lock);

    return idx;
}

int spnet_conn_state(int conn) {
    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(conn);
    int state = c ? c->state : -1;
    pthread_mutex_unlock(&g_lock);

    if (state == 0) {
        conn_poll_connect(conn);
        pthread_mutex_lock(&g_lock);
        c = conn_get_locked(conn);
        state = c ? c->state : -1;
        pthread_mutex_unlock(&g_lock);
    }

    return state;
}

int spnet_send(int conn, const void *buf, int len) {
    if (len <= 0) return 0;

    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(conn);
    int fd = -1, state = -1;
    if (c) { fd = c->fd; state = c->state; }
    pthread_mutex_unlock(&g_lock);

    if (fd < 0) return -1;
    if (state == 0) { conn_poll_connect(conn); return 0; }
    if (state == -1) return -1;

    ssize_t n = send(fd, buf, (size_t)len, MSG_NOSIGNAL);
    if (n >= 0) return (int)n;

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;

    pthread_mutex_lock(&g_lock);
    c = conn_get_locked(conn);
    if (c) c->state = -1;
    pthread_mutex_unlock(&g_lock);
    return -1;
}

int spnet_recv(int conn, void *buf, int len) {
    if (len <= 0) return 0;

    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(conn);
    int fd = -1, state = -1;
    if (c) { fd = c->fd; state = c->state; }
    pthread_mutex_unlock(&g_lock);

    if (fd < 0) return -1;
    if (state == 0) { conn_poll_connect(conn); return 0; }
    if (state == -1) return -1;

    ssize_t n = recv(fd, buf, (size_t)len, 0);
    if (n > 0) return (int)n;

    if (n == 0) {
        pthread_mutex_lock(&g_lock);
        c = conn_get_locked(conn);
        if (c) c->state = -1;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;

    pthread_mutex_lock(&g_lock);
    c = conn_get_locked(conn);
    if (c) c->state = -1;
    pthread_mutex_unlock(&g_lock);
    return -1;
}

void spnet_close(int conn) {
    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(conn);
    if (c) {
        if (c->fd >= 0) close(c->fd);
        c->used = 0;
        c->fd = -1;
        c->state = -1;
        c->is_guest_of_host = 0;
    }
    pthread_mutex_unlock(&g_lock);
}

#endif // !__vita__
