// Vita backend for sp-net.h: sceNet/sceNetCtl sockets for SPNET_MODE_LAN,
// sceNetAdhoc* for SPNET_MODE_ADHOC; empty translation unit off __vita__
#include "sp-net.h"

#ifdef __vita__

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <psp2/apputil.h>
#include <psp2/common_dialog.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/netcheck_dialog.h>
#include <psp2/pspnet_adhoc.h>
#include <psp2/pspnet_adhocctl.h>
#include <psp2/sysmodule.h>

#define SPNET_DISCOVERY_PORT     19741
#define SPNET_TCP_PORT           19742
#define SPNET_QUERY              "RSCV1?"
#define SPNET_QUERY_LEN          6
#define SPNET_REPLY_PREFIX       "RSCV1!"
#define SPNET_REPLY_PREFIX_LEN   6
#define SPNET_SCAN_WINDOW_MS     1500
#define SPNET_SCAN_RESEND_MS     300
#define SPNET_NET_CHECK_MS       1000
#define SPNET_MAX_SCAN_RESULTS   32
#define SPNET_CONN_TABLE_SIZE    (SPNET_MAX_GUESTS + 4)

// 9-byte ad-hoc id, used for adhocctlInit and the CONN dialog
#define SPNET_ADHOC_PRODUCT_ID   "RSCC00001"
#define SPNET_ADHOC_PTP_PORT     19743
// PDP discovery port; RSCV1 handshake carries world name + player count
#define SPNET_ADHOC_DISC_PORT    19744
// PTP buffer; 8192 fits 1 listen + 7 guests
#define SPNET_ADHOC_BUFSIZE      8192
// retransmit interval, must be >= 1s
#define SPNET_ADHOC_REXMT_INT_US 2000000 // 2 s
// rexmt_cnt; 5 = ~10s to drop a silent peer
#define SPNET_ADHOC_REXMT_CNT    5
// creation flag, must be 0 on Vita
#define SPNET_ADHOC_SOCK_FLAG    0
#define SPNET_MAX_ADHOC_PEERS    32

typedef struct {
    int used;
    int fd; // BSD socket fd (LAN) or sceNetAdhocPtp id (ADHOC)
    int state; // 0 connecting, 1 established, -1 dead
    int is_guest_of_host;
} spnet_conn_t;

// shared state, guarded by g_lock unless noted main-thread only
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static spnet_mode g_mode = SPNET_MODE_OFF;       // main-thread only
static spnet_status g_status = SPNET_STATUS_OFF; // main-thread only
static char g_error_text[80] = {0};

static spnet_conn_t g_conns[SPNET_CONN_TABLE_SIZE]; // lock

static int g_accept_queue[SPNET_MAX_GUESTS]; // lock
static int g_accept_head = 0, g_accept_tail = 0, g_accept_count = 0;

static spnet_world_info g_scan_results[SPNET_MAX_SCAN_RESULTS]; // lock
static int g_scan_count = 0;  // lock
static int g_scan_active = 0; // main-thread only
static uint64_t g_scan_t0 = 0;
static uint64_t g_scan_last_send = 0;
static int g_scan_pulses = 0; // broadcasts sent

static char g_host_name[64] = {0};
static int g_host_max_guests = 0;

// LAN backend state
static int g_net_ready = 0; // sceNet/sceNetCtl brought up by this module
// sceNet pool holds every socket's buffers
#define SPNET_NET_POOL_SIZE (2 * 1024 * 1024)
static char g_net_pool[SPNET_NET_POOL_SIZE] __attribute__((aligned(16)));
static int g_disc_sock = -1;
static int g_listen_sock = -1;
static uint64_t g_last_net_check = 0;

// ad-hoc backend state
static int g_adhoc_up = 0;     // sceNetAdhocInit done
static int g_adhoc_ctl_up = 0; // sceNetAdhocctlInit done
static SceNetEtherAddr g_own_mac;
static int g_adhoc_listen_id = -1;
static int g_pdp_disc = -1; // PDP discovery socket (host answers, guest queries)
static uint64_t g_last_scan_query_ms = 0; // throttle guest discovery broadcasts

// one CONN dialog joins the shared network when ad-hoc is enabled
static int g_dialog_open = 0;      // sceNetCheckDialogInit done, Term pending
static int g_adhoc_connected = 0;  // CONN dialog succeeded -> on the network
static SceNetAdhocctlGroupName g_dialog_group_name; // outlives Init..Term
static SceNpCommunicationId g_dialog_np_id;          // outlives Init..Term

// host-listen deferred until the CONN dialog succeeds; args stashed here
static int g_pending_host = 0;
static char g_pending_host_name[64];
static int g_pending_host_max_guests;

// small helpers

static uint64_t spnet_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static void spnet_set_nonblocking(int fd) {
    // SO_NONBLOCK makes the socket non-blocking
    int nonblock = 1;
    setsockopt(fd, SOL_SOCKET, SO_NONBLOCK, &nonblock, sizeof(nonblock));
}

// IPv4 dotted-quad to network-order 32-bit
static uint32_t spnet_ipv4(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
    return htonl(((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d);
}

static void spnet_addr_to_string(const struct in_addr *addr, char *out, size_t out_cap) {
    SceNetInAddr sce_addr;
    memcpy(&sce_addr, addr, sizeof(sce_addr));
    if (!sceNetInetNtop(SCE_NET_AF_INET, &sce_addr, out, (unsigned int)out_cap)) {
        snprintf(out, out_cap, "0.0.0.0");
    }
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

    SceNetInAddr sce_addr;
    if (sceNetInetPton(SCE_NET_AF_INET, ip, &sce_addr) != 1) return -1;
    memcpy(&out->sin_addr, &sce_addr, sizeof(struct in_addr));

    return 0;
}

// connection table; hold g_lock for the _locked variants

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

static void conn_mark_dead(int idx) {
    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(idx);
    if (c) c->state = -1;
    pthread_mutex_unlock(&g_lock);
}

// sweep established conns each pump, mark CLOSED ones dead
static uint64_t g_last_health_ms = 0;
static void adhoc_health_sweep(void) {
    uint64_t now = spnet_now_ms();
    if (g_last_health_ms != 0 && now - g_last_health_ms < 1000) return;
    g_last_health_ms = now;

    pthread_mutex_lock(&g_lock);
    // anything established?
    int any = 0;
    for (int i = 0; i < SPNET_CONN_TABLE_SIZE; i++) {
        if (g_conns[i].used && g_conns[i].state == 1 && g_conns[i].fd >= 0) { any = 1; break; }
    }
    if (!any) { pthread_mutex_unlock(&g_lock); return; }

    int buflen = 0;
    if (sceNetAdhocGetPtpStat(&buflen, NULL) >= 0 && buflen > 0) {
        static uint8_t raw[SPNET_MAX_ADHOC_PEERS * sizeof(SceNetAdhocPtpStat)];
        if ((size_t)buflen > sizeof(raw)) buflen = (int)sizeof(raw);
        if (sceNetAdhocGetPtpStat(&buflen, raw) >= 0) {
            for (int i = 0; i < SPNET_CONN_TABLE_SIZE; i++) {
                spnet_conn_t *c = &g_conns[i];
                if (!c->used || c->state != 1 || c->fd < 0) continue;
                SceNetAdhocPtpStat *p = (SceNetAdhocPtpStat *)raw;
                while (p != NULL) {
                    if (p->id == c->fd) {
                        // only CLOSED means dead
                        if (p->state == SCE_NET_ADHOC_PTP_STATE_CLOSED) {
                            c->state = -1;
                        }
                        break;
                    }
                    p = p->next;
                }
            }
        }
    }
    pthread_mutex_unlock(&g_lock);
}

// advance a still-connecting handle once connect resolves
static void conn_poll_connect(int idx) {
    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(idx);
    int fd = -1;
    if (c && c->state == 0) fd = c->fd;
    pthread_mutex_unlock(&g_lock);

    if (fd < 0) return;

    int new_state = 0;

    if (g_mode == SPNET_MODE_ADHOC) {
        // GetPtpStat is one global query; g_lock serializes it
        pthread_mutex_lock(&g_lock);
        int buflen = 0;
        if (sceNetAdhocGetPtpStat(&buflen, NULL) >= 0 && buflen > 0) {
            static uint8_t raw[SPNET_MAX_ADHOC_PEERS * sizeof(SceNetAdhocPtpStat)];
            if ((size_t)buflen > sizeof(raw)) buflen = (int)sizeof(raw);

            if (sceNetAdhocGetPtpStat(&buflen, raw) >= 0) {
                SceNetAdhocPtpStat *p = (SceNetAdhocPtpStat *)raw;
                while (p != NULL) {
                    if (p->id == fd) {
                        if (p->state == SCE_NET_ADHOC_PTP_STATE_ESTABLISHED) new_state = 1;
                        else if (p->state == SCE_NET_ADHOC_PTP_STATE_CLOSED) new_state = -1;
                        break;
                    }
                    p = p->next;
                }
            }
        }
        pthread_mutex_unlock(&g_lock);
    } else if (g_mode == SPNET_MODE_LAN) {
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval tv = {0, 0};

        int r = select(fd + 1, NULL, &wfds, NULL, &tv);
        if (r > 0 && FD_ISSET(fd, &wfds)) {
            int err = 0;
            socklen_t elen = sizeof(err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen);
            new_state = (err == 0) ? 1 : -1;
        } else if (r < 0) {
            new_state = -1;
        }
    }

    if (new_state != 0) {
        pthread_mutex_lock(&g_lock);
        c = conn_get_locked(idx);
        if (c && c->state == 0) c->state = new_state;
        pthread_mutex_unlock(&g_lock);
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

// auto-revert SCANNING to READY after the window; LAN resends the broadcast
static void lan_send_query(void);

static void pump_scan_timeout(void) {
    if (!g_scan_active) return;

    uint64_t now = spnet_now_ms();

    if (g_mode == SPNET_MODE_LAN && now - g_scan_last_send >= SPNET_SCAN_RESEND_MS) {
        lan_send_query();
        g_scan_last_send = now;
    }

    if (now - g_scan_t0 >= SPNET_SCAN_WINDOW_MS) {
        g_scan_active = 0;
        if (g_status == SPNET_STATUS_SCANNING) g_status = SPNET_STATUS_READY;
    }
}

// LAN backend

static void spnet_vita_net_bringup(void) {
    if (g_net_ready) return;

    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceSysmoduleLoadModule(NET): 0x%08x (may "
                        "already be resident)\n", (unsigned)ret);
    }

    SceNetInitParam net_param;
    net_param.memory = g_net_pool;
    net_param.size = sizeof(g_net_pool);
    net_param.flags = 0;

    ret = sceNetInit(&net_param);
    if (ret < 0) {
        // non-fatal; the socket calls below are the real gate
        fprintf(stderr, "[spnet] sceNetInit: 0x%08x (continuing; stack may "
                        "already be initialised elsewhere)\n", (unsigned)ret);
    }

    ret = sceNetCtlInit();
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceNetCtlInit: 0x%08x (continuing)\n", (unsigned)ret);
    }

    g_net_ready = 1;
}

static int spnet_vita_connected(void) {
    int state = 0;
    if (sceNetCtlInetGetState(&state) < 0) return 0;
    return state == SCE_NETCTL_STATE_CONNECTED;
}

static void pump_lan_connectivity(void) {
    uint64_t now = spnet_now_ms();
    if (now - g_last_net_check < SPNET_NET_CHECK_MS) return;
    g_last_net_check = now;

    if (!spnet_vita_connected()) {
        if (g_status == SPNET_STATUS_HOSTING) {
            // connectivity lost mid-session: tear down listen/discovery sockets
            if (g_listen_sock >= 0) { close(g_listen_sock); g_listen_sock = -1; }
            if (g_disc_sock >= 0) { close(g_disc_sock); g_disc_sock = -1; }
        }
        if (g_status != SPNET_STATUS_OFF) g_status = SPNET_STATUS_NO_NETWORK;
    } else if (g_status == SPNET_STATUS_NO_NETWORK || g_status == SPNET_STATUS_STARTING) {
        g_status = SPNET_STATUS_READY;
    }
}

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

    char ip[64];
    spnet_addr_to_string(&from->sin_addr, ip, sizeof(ip));
    char addr[64];
    snprintf(addr, sizeof(addr), "%.50s:%d", ip, port);

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

// three-target broadcast: LAN broadcast, loopback broadcast, unicast fallbacks
static void lan_send_query(void) {
    if (g_disc_sock < 0) return;

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(SPNET_DISCOVERY_PORT);

    dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    sendto(g_disc_sock, SPNET_QUERY, SPNET_QUERY_LEN, 0, (struct sockaddr *)&dst, sizeof(dst));

    dst.sin_addr.s_addr = spnet_ipv4(127, 255, 255, 255);
    sendto(g_disc_sock, SPNET_QUERY, SPNET_QUERY_LEN, 0, (struct sockaddr *)&dst, sizeof(dst));

    dst.sin_addr.s_addr = spnet_ipv4(127, 0, 0, 1);
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
            close(fd);
            g_conns[idx].used = 0;
        }
        pthread_mutex_unlock(&g_lock);
    }
}

static int lan_scan_start(void) {
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
    lan_send_query();
    g_scan_last_send = g_scan_t0;

    if (g_status != SPNET_STATUS_HOSTING) g_status = SPNET_STATUS_SCANNING;

    return 0;
}

static int lan_host_start(const char *display_name, int max_guests) {
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

static void lan_host_stop(void) {
    if (g_listen_sock >= 0) { close(g_listen_sock); g_listen_sock = -1; }
    if (g_disc_sock >= 0) { close(g_disc_sock); g_disc_sock = -1; }

    pthread_mutex_lock(&g_lock);
    g_accept_head = g_accept_tail = g_accept_count = 0;
    pthread_mutex_unlock(&g_lock);

    g_status = SPNET_STATUS_READY;
}

static int lan_connect(const char *address) {
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

// ad-hoc backend

// PDP discovery: RSCV1 handshake finds actual hosts; address = host MAC

// refresh g_own_mac before every socket create
static void adhoc_refresh_own_mac(const char *who) {
    (void)who;
    (void)sceNetAdhocctlGetEtherAddr(&g_own_mac);
}

static void adhoc_pdp_open(void) {
    if (g_pdp_disc >= 0) return;
    adhoc_refresh_own_mac("PdpCreate");
    int id = sceNetAdhocPdpCreate(&g_own_mac, SPNET_ADHOC_DISC_PORT, 0x2000, 0);
    if (id < 0) {
        // discovery dead if this fails; surface it on the status line
        fprintf(stderr, "[spnet] sceNetAdhocPdpCreate failed: 0x%08x\n", (unsigned)id);
        g_status = SPNET_STATUS_ERROR;
        snprintf(g_error_text, sizeof(g_error_text),
                 "Ad-hoc discovery failed (0x%08x)", (unsigned)id);
        return;
    }
    g_pdp_disc = id;
}

static void adhoc_pdp_close(void) {
    if (g_pdp_disc >= 0) {
        sceNetAdhocPdpDelete(g_pdp_disc, 0);
        g_pdp_disc = -1;
    }
}

// record a name|players reply into the scan list, dedup by MAC
static void adhoc_pdp_record_reply(const char *payload, const SceNetEtherAddr *from) {
    const char *bar = strchr(payload, '|');
    if (!bar) return;
    char name[64];
    size_t nlen = (size_t)(bar - payload);
    if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
    memcpy(name, payload, nlen);
    name[nlen] = '\0';
    int players = atoi(bar + 1);

    char mac_str[32];
    sceNetEtherNtostr((SceNetEtherAddr *)from, mac_str, sizeof(mac_str));

    pthread_mutex_lock(&g_lock);
    int idx = -1;
    for (int i = 0; i < g_scan_count; i++) {
        if (strcmp(g_scan_results[i].address, mac_str) == 0) { idx = i; break; }
    }
    if (idx < 0 && g_scan_count < SPNET_MAX_SCAN_RESULTS) idx = g_scan_count++;
    if (idx >= 0) {
        snprintf(g_scan_results[idx].name, sizeof(g_scan_results[idx].name), "%.63s", name);
        snprintf(g_scan_results[idx].address, sizeof(g_scan_results[idx].address), "%s", mac_str);
        g_scan_results[idx].player_count = players;
    }
    pthread_mutex_unlock(&g_lock);
}

// drive PDP discovery each frame: host replies to queries, guest records replies
static void pump_adhoc_pdp(void) {
    if (g_pdp_disc < 0) return;
    for (;;) {
        SceNetEtherAddr from;
        SceUShort16 fport = 0;
        char buf[256];
        int len = (int)sizeof(buf) - 1;
        int r = sceNetAdhocPdpRecv(g_pdp_disc, &from, &fport, buf, &len, 0,
                                   SCE_NET_ADHOC_F_NONBLOCK);
        if (r < 0 || len <= 0) break;
        buf[len] = '\0';

        if (g_status == SPNET_STATUS_HOSTING && len == SPNET_QUERY_LEN &&
            memcmp(buf, SPNET_QUERY, SPNET_QUERY_LEN) == 0) {
            pthread_mutex_lock(&g_lock);
            int guests = conn_count_established_guests_locked();
            char name[64];
            snprintf(name, sizeof(name), "%s", g_host_name);
            pthread_mutex_unlock(&g_lock);

            char reply[160];
            int rl = snprintf(reply, sizeof(reply), "%s%s|%d",
                              SPNET_REPLY_PREFIX, name, guests);
            if (rl > 0) {
                sceNetAdhocPdpSend(g_pdp_disc, &from, fport, reply, rl, 0,
                                   SCE_NET_ADHOC_F_NONBLOCK);
            }
        } else if (g_scan_active && len > SPNET_REPLY_PREFIX_LEN &&
                   memcmp(buf, SPNET_REPLY_PREFIX, SPNET_REPLY_PREFIX_LEN) == 0) {
            adhoc_pdp_record_reply(buf + SPNET_REPLY_PREFIX_LEN, &from);
        }
    }
}

// guest scan: broadcast the discovery query, throttled ~1/s
static void pump_adhoc_scan(void) {
    if (!g_scan_active || g_pdp_disc < 0) return;
    uint64_t now = spnet_now_ms();
    if (g_last_scan_query_ms != 0 && now - g_last_scan_query_ms < 1000) return;
    g_last_scan_query_ms = now;

    SceNetEtherAddr bcast;
    memset(&bcast, 0xFF, sizeof(bcast));
    sceNetAdhocPdpSend(g_pdp_disc, &bcast, SPNET_ADHOC_DISC_PORT, SPNET_QUERY,
                       SPNET_QUERY_LEN, 0, SCE_NET_ADHOC_F_NONBLOCK);
    g_scan_pulses++; // one per ~1s broadcast
}

// NetCheckDialog: Init/poll/GetResult/Term flow

// start the ad-hoc-connect dialog: empty group, product id, PSP_ADHOC_CONN, timeoutUs 0
static int adhoc_dialog_start(void) {
    memset(&g_dialog_group_name, 0, sizeof(g_dialog_group_name)); // empty

    memset(&g_dialog_np_id, 0, sizeof(g_dialog_np_id));
    memcpy(g_dialog_np_id.data, SPNET_ADHOC_PRODUCT_ID, 9);
    g_dialog_np_id.term = '\0';
    g_dialog_np_id.num = 0;

    SceNetCheckDialogParam param;
    sceNetCheckDialogParamInit(&param);
    param.groupName = &g_dialog_group_name;
    param.npCommunicationId = g_dialog_np_id;
    param.mode = SCE_NETCHECK_DIALOG_MODE_PSP_ADHOC_CONN;
    param.timeoutUs = 0;

    int ret = sceNetCheckDialogInit(&param);
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceNetCheckDialogInit(CONN): 0x%08x\n",
               (unsigned)ret);
        return -1;
    }

    g_dialog_open = 1;
    return 0;
}

// teardown an in-flight dialog: Abort then Term, non-blocking
static void adhoc_dialog_teardown(void) {
    if (!g_dialog_open) {
        return;
    }

    if (sceNetCheckDialogGetStatus() == SCE_COMMON_DIALOG_STATUS_RUNNING) {
        sceNetCheckDialogAbort();
    }
    sceNetCheckDialogTerm();

    g_dialog_open = 0;
}

// open the host PTP listen socket, sets HOSTING on success
static int adhoc_do_listen(const char *name, int max_guests) {
    adhoc_refresh_own_mac("PtpListen");
    int lid = sceNetAdhocPtpListen(&g_own_mac, SPNET_ADHOC_PTP_PORT,
                                   SPNET_ADHOC_BUFSIZE, SPNET_ADHOC_REXMT_INT_US,
                                   SPNET_ADHOC_REXMT_CNT, max_guests,
                                   SPNET_ADHOC_SOCK_FLAG);
    if (lid < 0) {
        fprintf(stderr, "[spnet] sceNetAdhocPtpListen failed: 0x%08x "
                "(port=%d bufsize=%d rexmt_int=%d rexmt_cnt=%d backlog=%d flag=%d)\n",
                (unsigned)lid, SPNET_ADHOC_PTP_PORT, SPNET_ADHOC_BUFSIZE,
                SPNET_ADHOC_REXMT_INT_US, SPNET_ADHOC_REXMT_CNT, max_guests,
                SPNET_ADHOC_SOCK_FLAG);
        g_status = SPNET_STATUS_ERROR;
        snprintf(g_error_text, sizeof(g_error_text), "Ad-hoc listen failed (0x%08x)",
                (unsigned)lid);
        return -1;
    }
    g_adhoc_listen_id = lid;
    g_host_max_guests = max_guests;
    snprintf(g_host_name, sizeof(g_host_name), "%s", name);
    g_status = SPNET_STATUS_HOSTING;
    return 0;
}

// advance the CONN dialog; success starts discovery + deferred PtpListen, cancel falls back to solo
static void pump_adhoc_dialog(void) {
    if (!g_dialog_open) return;
    if (sceNetCheckDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) return;

    SceNetCheckDialogResult result;
    memset(&result, 0, sizeof(result));
    int gret = sceNetCheckDialogGetResult(&result);
    sceNetCheckDialogTerm();
    g_dialog_open = 0;

    int success = gret >= 0 && result.result == 0;
    if (!success) {
        // cancelled or failed: report NO_NETWORK
        g_adhoc_connected = 0;
        g_pending_host = 0;
        g_status = SPNET_STATUS_NO_NETWORK;
        return;
    }

    g_adhoc_connected = 1;

    // every socket create re-fetches g_own_mac first
    adhoc_pdp_open(); // discovery channel: host answers, guest queries

    // deferred host listen
    if (g_pending_host) {
        g_pending_host = 0;
        if (adhoc_do_listen(g_pending_host_name, g_pending_host_max_guests) == 0) {
            return; // HOSTING
        }
        return; // ERROR set by adhoc_do_listen
    }

    // guest: start live peer discovery
    g_scan_active = 1;
    g_scan_t0 = spnet_now_ms();
    pump_adhoc_scan();
    g_status = SPNET_STATUS_SCANNING;
}

static int adhoc_scan_start(void) {
    // refresh re-queries the peer list, no dialog
    if (g_status == SPNET_STATUS_HOSTING) return 0;
    if (!g_adhoc_connected) return 0;

    // clear the old list and force an immediate broadcast
    pthread_mutex_lock(&g_lock);
    g_scan_count = 0;
    pthread_mutex_unlock(&g_lock);
    g_last_scan_query_ms = 0;

    g_scan_active = 1;
    g_scan_t0 = spnet_now_ms();
    pump_adhoc_scan();
    g_status = SPNET_STATUS_SCANNING;
    return 0;
}

static int adhoc_host_start(const char *display_name, int max_guests) {
    int backlog = max_guests > 0 ? max_guests : SPNET_MAX_GUESTS;
    if (backlog > SPNET_MAX_GUESTS) backlog = SPNET_MAX_GUESTS;
    const char *name = display_name ? display_name : "single-player world";

    // PtpListen now if already on the network, else defer until the CONN dialog lands
    if (g_adhoc_connected) {
        return adhoc_do_listen(name, backlog);
    }

    g_pending_host = 1;
    snprintf(g_pending_host_name, sizeof(g_pending_host_name), "%s", name);
    g_pending_host_max_guests = backlog;
    g_status = SPNET_STATUS_STARTING;
    return 0;
}

static void adhoc_host_stop(void) {
    adhoc_dialog_teardown(); // no-op unless a CREATE dialog is still up

    if (g_adhoc_listen_id >= 0) {
        sceNetAdhocPtpClose(g_adhoc_listen_id, 0);
        g_adhoc_listen_id = -1;
    }

    pthread_mutex_lock(&g_lock);
    g_accept_head = g_accept_tail = g_accept_count = 0;
    pthread_mutex_unlock(&g_lock);

    g_status = SPNET_STATUS_READY;
}

static void pump_adhoc_accept(void) {
    if (g_adhoc_listen_id < 0) return;

    for (;;) {
        SceNetEtherAddr peer_addr;
        SceUShort16 peer_port = 0;
        int newid = sceNetAdhocPtpAccept(g_adhoc_listen_id, &peer_addr, &peer_port, 0,
                                         SCE_NET_ADHOC_F_NONBLOCK);
        if (newid < 0) break; // SCE_ERROR_NET_ADHOC_WOULD_BLOCK or nothing pending

        pthread_mutex_lock(&g_lock);
        int idx = conn_alloc_locked();
        if (idx < 0) {
            pthread_mutex_unlock(&g_lock);
            sceNetAdhocPtpClose(newid, 0);
            continue;
        }

        g_conns[idx].fd = newid;
        g_conns[idx].state = 1;
        g_conns[idx].is_guest_of_host = 1;

        if (g_accept_count < SPNET_MAX_GUESTS) {
            g_accept_queue[g_accept_tail] = idx;
            g_accept_tail = (g_accept_tail + 1) % SPNET_MAX_GUESTS;
            g_accept_count++;
        } else {
            sceNetAdhocPtpClose(newid, 0);
            g_conns[idx].used = 0;
        }
        pthread_mutex_unlock(&g_lock);
    }
}

static int adhoc_connect(const char *address) {
    SceNetEtherAddr dest;
    if (!address || sceNetEtherStrton(address, &dest) < 0) return -1;

    adhoc_refresh_own_mac("PtpOpen");
    int id = sceNetAdhocPtpOpen(&g_own_mac, 0, &dest, SPNET_ADHOC_PTP_PORT,
                                SPNET_ADHOC_BUFSIZE, SPNET_ADHOC_REXMT_INT_US,
                                SPNET_ADHOC_REXMT_CNT, SPNET_ADHOC_SOCK_FLAG);
    if (id < 0) {
        fprintf(stderr, "[spnet] sceNetAdhocPtpOpen failed: 0x%08x "
                "(dport=%d bufsize=%d rexmt_int=%d rexmt_cnt=%d flag=%d)\n",
                (unsigned)id, SPNET_ADHOC_PTP_PORT, SPNET_ADHOC_BUFSIZE,
                SPNET_ADHOC_REXMT_INT_US, SPNET_ADHOC_REXMT_CNT,
                SPNET_ADHOC_SOCK_FLAG);
        return -1;
    }

    pthread_mutex_lock(&g_lock);
    int idx = conn_alloc_locked();
    if (idx < 0) {
        pthread_mutex_unlock(&g_lock);
        sceNetAdhocPtpClose(id, 0);
        return -1;
    }
    g_conns[idx].fd = id;
    g_conns[idx].is_guest_of_host = 0;
    g_conns[idx].state = 0; // connecting; polled via conn_poll_connect
    pthread_mutex_unlock(&g_lock);

    // kick off the non-blocking handshake; completion polls via conn_poll_connect
    sceNetAdhocPtpConnect(id, 0, SCE_NET_ADHOC_F_NONBLOCK);

    return idx;
}

// sceAppUtilInit must run before sceNetCheckDialogInit; runs once
static int g_apputil_ready = 0;
static void adhoc_apputil_bringup(void) {
    if (g_apputil_ready) return;

    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_APPUTIL);
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceSysmoduleLoadModule(APPUTIL): 0x%08x (may "
                        "already be resident)\n", (unsigned)ret);
    }

    SceAppUtilInitParam init_param;
    SceAppUtilBootParam boot_param;
    memset(&init_param, 0, sizeof(init_param));
    memset(&boot_param, 0, sizeof(boot_param));
    ret = sceAppUtilInit(&init_param, &boot_param);
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceAppUtilInit: 0x%08x (continuing; may "
                        "already be initialised)\n", (unsigned)ret);
    }

    g_apputil_ready = 1;
}

static int adhoc_init(void) {
    g_status = SPNET_STATUS_STARTING;

    // base sceNet must be up before sceNetAdhocInit
    spnet_vita_net_bringup();

    // the CONN dialog needs the app-utility subsystem up
    adhoc_apputil_bringup();

    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_PSPNET_ADHOC);
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceSysmoduleLoadModule(PSPNET_ADHOC): 0x%08x\n",
               (unsigned)ret);
        g_status = SPNET_STATUS_ERROR;
        snprintf(g_error_text, sizeof(g_error_text), "Ad-hoc module load failed");
        return -1;
    }

    ret = sceNetAdhocInit();
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceNetAdhocInit failed: 0x%08x\n", (unsigned)ret);
        g_status = SPNET_STATUS_ERROR;
        snprintf(g_error_text, sizeof(g_error_text), "Ad-hoc init failed (0x%08x)",
                (unsigned)ret);
        return -1;
    }
    g_adhoc_up = 1;

    // adhocctlInit keyed by the product id, type RESERVED
    SceNetAdhocctlAdhocId adhoc_id;
    memset(&adhoc_id, 0, sizeof(adhoc_id));
    adhoc_id.type = SCE_NET_ADHOCCTL_ADHOCTYPE_RESERVED;
    memcpy(adhoc_id.data, SPNET_ADHOC_PRODUCT_ID, SCE_NET_ADHOCCTL_ADHOCID_LEN);

    ret = sceNetAdhocctlInit(&adhoc_id);
    if (ret < 0) {
        fprintf(stderr, "[spnet] sceNetAdhocctlInit failed: 0x%08x\n", (unsigned)ret);
        g_status = SPNET_STATUS_ERROR;
        snprintf(g_error_text, sizeof(g_error_text), "Ad-hoc bring-up failed (0x%08x)",
                (unsigned)ret);
        return -1;
    }
    g_adhoc_ctl_up = 1;

    // fetch the device's own adapter MAC (self address); never abort on failure
    ret = sceNetAdhocctlGetEtherAddr(&g_own_mac);
    (void)ret;

    g_dialog_open = 0;
    g_adhoc_connected = 0;
    g_pending_host = 0;

    // enabling ad-hoc joins the shared network via the CONN dialog; async via spnet_pump
    if (adhoc_dialog_start() != 0) {
        g_status = SPNET_STATUS_ERROR;
        snprintf(g_error_text, sizeof(g_error_text), "Ad-hoc connect dialog failed");
        return -1;
    }

    g_status = SPNET_STATUS_STARTING; // CONN dialog up
    return 0;
}

static int lan_init(void) {
    spnet_vita_net_bringup();
    g_status = spnet_vita_connected() ? SPNET_STATUS_READY : SPNET_STATUS_NO_NETWORK;
    g_last_net_check = spnet_now_ms();
    return 0;
}

// public API, see sp-net.h for the contract

int spnet_init(spnet_mode mode) {
    spnet_shutdown();

    g_error_text[0] = '\0';

    if (mode == SPNET_MODE_OFF) {
        g_mode = SPNET_MODE_OFF;
        g_status = SPNET_STATUS_OFF;
        return 0;
    }

    if (mode == SPNET_MODE_LAN) {
        g_mode = SPNET_MODE_LAN;
        return lan_init();
    }

    if (mode == SPNET_MODE_ADHOC) {
        g_mode = SPNET_MODE_ADHOC;
        return adhoc_init();
    }

    return -1;
}

void spnet_shutdown(void) {
    int was_adhoc = (g_mode == SPNET_MODE_ADHOC);

    if (g_mode == SPNET_MODE_LAN) {
        if (g_disc_sock >= 0) { close(g_disc_sock); g_disc_sock = -1; }
        if (g_listen_sock >= 0) { close(g_listen_sock); g_listen_sock = -1; }
    } else if (was_adhoc) {
        adhoc_dialog_teardown(); // no-op unless the CONN dialog is still up
        adhoc_pdp_close();       // discovery PDP socket

        if (g_adhoc_listen_id >= 0) {
            sceNetAdhocPtpClose(g_adhoc_listen_id, 0);
            g_adhoc_listen_id = -1;
        }
        g_adhoc_connected = 0;
        g_pending_host = 0;
        g_last_scan_query_ms = 0;
    }

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < SPNET_CONN_TABLE_SIZE; i++) {
        if (g_conns[i].used && g_conns[i].fd >= 0) {
            if (was_adhoc) sceNetAdhocPtpClose(g_conns[i].fd, 0);
            else close(g_conns[i].fd);
        }
        g_conns[i].used = 0;
        g_conns[i].fd = -1;
        g_conns[i].state = -1;
        g_conns[i].is_guest_of_host = 0;
    }
    g_accept_head = g_accept_tail = g_accept_count = 0;
    g_scan_count = 0;
    pthread_mutex_unlock(&g_lock);

    // full ad-hoc teardown; the shared LAN sceNet stack is never terminated
    if (was_adhoc) {
        // leave the system ad-hoc network the CONN dialog joined
        (void)sceNetCtlAdhocDisconnect();
        if (g_adhoc_ctl_up) { (void)sceNetAdhocctlTerm(); g_adhoc_ctl_up = 0; }
        if (g_adhoc_up) { (void)sceNetAdhocTerm(); g_adhoc_up = 0; }

        // radio is free now; re-init netctl to start re-associating Wi-Fi while in menus
        sceNetCtlTerm(); // returns void
        (void)sceNetCtlInit();
    }

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

    // NetCheck dialog on screen; overrides the status text below
    if (g_mode == SPNET_MODE_ADHOC && g_dialog_open) {
        return "Connecting to ad-hoc...";
    }

    switch (g_status) {
    case SPNET_STATUS_OFF: return "Not connected";
    case SPNET_STATUS_STARTING:
        return g_mode == SPNET_MODE_ADHOC ? "Opening ad-hoc..." : "Connecting...";
    case SPNET_STATUS_NO_NETWORK: return "Not connected";
    case SPNET_STATUS_READY: return "Connected";
    case SPNET_STATUS_SCANNING:
        if (g_mode == SPNET_MODE_ADHOC) {
            pthread_mutex_lock(&g_lock);
            int n = g_scan_count;
            pthread_mutex_unlock(&g_lock);
            if (n > 0) {
                snprintf(buf, sizeof(buf), "Connected - %d world%s found", n,
                         n == 1 ? "" : "s");
                return buf;
            }
            // on the network, nothing found yet: Connected plus a spinner
            snprintf(buf, sizeof(buf), "Connected - searching %c",
                     "|/-\\"[g_scan_pulses & 3]);
            return buf;
        }
        return "Scanning...";
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
    if (g_mode == SPNET_MODE_LAN) {
        pump_lan_connectivity();
        if (g_status != SPNET_STATUS_NO_NETWORK && g_status != SPNET_STATUS_ERROR) {
            pump_discovery();
            pump_accept();
        }
        pump_scan_timeout();
        pump_connecting();
    } else if (g_mode == SPNET_MODE_ADHOC) {
        pump_adhoc_dialog();
        pump_adhoc_accept();
        pump_adhoc_pdp();  // host answers discovery queries / guest collects replies
        pump_adhoc_scan(); // guest re-broadcasts the query (throttled)
        adhoc_health_sweep(); // detect departed guests/host -> mark conn dead
        // no pump_scan_timeout(): group membership persists, discovery runs indefinitely
        pump_connecting();
    }
}

int spnet_dialog_active(void) {
    return g_mode == SPNET_MODE_ADHOC && g_dialog_open;
}

int spnet_scan_start(void) {
    if (g_mode == SPNET_MODE_LAN) return lan_scan_start();
    if (g_mode == SPNET_MODE_ADHOC) return adhoc_scan_start();
    return -1;
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
    if (g_mode == SPNET_MODE_LAN) return lan_host_start(display_name, max_guests);
    if (g_mode == SPNET_MODE_ADHOC) return adhoc_host_start(display_name, max_guests);
    return -1;
}

void spnet_host_stop(void) {
    if (g_mode == SPNET_MODE_LAN) lan_host_stop();
    else if (g_mode == SPNET_MODE_ADHOC) adhoc_host_stop();
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
    if (g_mode == SPNET_MODE_LAN) return lan_connect(address);
    if (g_mode == SPNET_MODE_ADHOC) return adhoc_connect(address);
    return -1;
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

    if (g_mode == SPNET_MODE_ADHOC) {
        int slen = len;
        int ret = sceNetAdhocPtpSend(fd, buf, &slen, 0, SCE_NET_ADHOC_F_NONBLOCK);
        if (ret >= 0) return slen;
        if (ret == SCE_ERROR_NET_ADHOC_WOULD_BLOCK) return 0;
        conn_mark_dead(conn);
        return -1;
    }

    // LAN: plain send(), no MSG_NOSIGNAL in the VitaSDK headers
    ssize_t n = send(fd, buf, (size_t)len, 0);
    if (n >= 0) return (int)n;
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    conn_mark_dead(conn);
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

    if (g_mode == SPNET_MODE_ADHOC) {
        int rlen = len;
        int ret = sceNetAdhocPtpRecv(fd, buf, &rlen, 0, SCE_NET_ADHOC_F_NONBLOCK);
        if (ret >= 0) return rlen;
        if (ret == SCE_ERROR_NET_ADHOC_WOULD_BLOCK) return 0;
        conn_mark_dead(conn);
        return -1;
    }

    ssize_t n = recv(fd, buf, (size_t)len, 0);
    if (n > 0) return (int)n;
    if (n == 0) { conn_mark_dead(conn); return -1; }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return 0;
    conn_mark_dead(conn);
    return -1;
}

void spnet_close(int conn) {
    pthread_mutex_lock(&g_lock);
    spnet_conn_t *c = conn_get_locked(conn);
    int fd = -1;
    int mode_was_adhoc = (g_mode == SPNET_MODE_ADHOC);
    if (c) {
        fd = c->fd;
        c->used = 0;
        c->fd = -1;
        c->state = -1;
        c->is_guest_of_host = 0;
    }
    pthread_mutex_unlock(&g_lock);

    if (fd >= 0) {
        if (mode_was_adhoc) sceNetAdhocPtpClose(fd, 0);
        else close(fd);
    }
}

#endif // __vita__
