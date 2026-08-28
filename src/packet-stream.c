#include "packet-stream.h"
#include "protocol177.h"

#ifdef WITH_SINGLEPLAYER
#include "singleplayer.h"
#include "sp-net.h"
#endif

#ifdef HAVE_SIGNALS
#include <signal.h>
#endif

#ifdef _WIN32
#define close closesocket
#define ioctl ioctlsocket

static int winsock_init = 0;
#endif

#ifdef WII
#define socket(x, y, z) net_socket(x, y, z)
#define gethostbyname net_gethostbyname
#define setsockopt net_setsockopt
#define select net_select
#define connect net_connect
#define close net_close
#define write net_write
#define recv net_recv
#define ioctl net_ioctl
#endif

#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>

// sockets ride sceNet, initialised once with its own pool; DNS uses sceNetResolver, not getaddrinfo
#define VITA_NET_POOL_SIZE (1 * 1024 * 1024)
// max wait for Wi-Fi infra to report "connected"; only paid when not already
// connected. Sized for re-associating with the AP after an ad-hoc co-op session
// (which drops infra, see vita_net_connected) -- a fresh association can take 10-15s
#define VITA_NET_CONNECT_WAIT_MS 20000
static int vita_net_ready = 0;
static char vita_net_pool[VITA_NET_POOL_SIZE] __attribute__((aligned(16)));

// bring up sceNet once, 0 on success; guarded by vita_net_ready
static int vita_net_init(void) {
    if (vita_net_ready) {
        return 0;
    }

    int ret = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (ret < 0) {
        // not fatal alone, the module may already be resident; sceNetInit below is the real gate
        mud_error("vita: sceSysmoduleLoadModule(NET): 0x%08x\n", (unsigned)ret);
    }

    SceNetInitParam net_param;
    net_param.memory = vita_net_pool;
    net_param.size = sizeof(vita_net_pool);
    net_param.flags = 0;

    ret = sceNetInit(&net_param);
    if (ret < 0) {
        // EBUSY means the co-op transport already brought the stack up, reuse it; any other code is a real failure
        if ((unsigned)ret != 0x80410110u) { // SCE_NET_ERROR_EBUSY = reuse it
            mud_error("vita: sceNetInit failed: 0x%08x\n", (unsigned)ret);
            return -1;
        }
    }

    ret = sceNetCtlInit();
    if (ret < 0 && (unsigned)ret != 0x80412102u) {
        // 0x80412102 = SCE_NET_CTL_ERROR_NOT_TERMINATED: netctl already up (the SP
        // co-op transport brought it up and never tears it down) -- reuse it, like
        // the sceNetInit EBUSY case above. Any other failure only matters for the
        // connectivity probe below, which then reports "offline".
        mud_error("vita: sceNetCtlInit: 0x%08x\n", (unsigned)ret);
    }

    vita_net_ready = 1;
    return 0;
}

// 1 when Wi-Fi is associated with an IP. If infra is idle-disconnected (e.g. after
// an ad-hoc co-op session), re-inits netctl to re-associate, then waits it out
static int vita_net_connected(void) {
    int state = 0;
    int ret = sceNetCtlInetGetState(&state);
    if (ret < 0) {
        mud_error("[net] sceNetCtlInetGetState failed: 0x%08x\n", (unsigned)ret);
        return 0;
    }
    if (state == SCE_NETCTL_STATE_CONNECTED) {
        return 1;
    }

    // idle-disconnected: re-init netctl to re-drive the AP association. Only when
    // fully idle (state 0); re-initing mid-association (1/2) would abort the connect
    if (state == 0) {
        mud_error("[net] infra idle (state=0) before online connect; "
                  "re-initialising netctl to re-associate with Wi-Fi\n");
        sceNetCtlTerm(); // returns void
        int i = sceNetCtlInit();
        mud_error("[net] netctl reinit: Init=0x%08x\n", (unsigned)i);
    }

    int last_state = -1;
    for (int waited_ms = 0; waited_ms <= VITA_NET_CONNECT_WAIT_MS;
         waited_ms += 100) {
        int st = 0;
        ret = sceNetCtlInetGetState(&st);
        if (ret < 0) {
            mud_error("[net] sceNetCtlInetGetState failed: 0x%08x\n",
                      (unsigned)ret);
            return 0;
        }

        if (st != last_state) { // log each state transition
            mud_error("[net] infra wait: t=%dms state=%d\n", waited_ms, st);
            last_state = st;
        }

        if (st == SCE_NETCTL_STATE_CONNECTED) {
            return 1;
        }

        delay_ticks(100);
    }

    mud_error("[net] no connection after %dms (netctl state=%d; "
              "0=disconnected 1=connecting 2=finalising 3=connected)\n",
              VITA_NET_CONNECT_WAIT_MS, last_state);
    return 0;
}
#endif

#if 0
char *SPOOKY_THREAT =
    "All RuneScape code and data, including this message, are copyright 2003 "
    "Jagex Ltd. Unauthorised reproduction in any form is strictly prohibited.  "
    "The RuneScape network protocol is copyright 2003 Jagex Ltd and is "
    "protected by international copyright laws. The RuneScape network protocol "
    "also incorporates a copy protection mechanism to prevent unauthorised "
    "access or use of our servers. Attempting to break, bypass or duplicate "
    "this mechanism is an infringement of the Digital Millienium Copyright Act "
    "and may lead to prosecution. Decompiling, or reverse-engineering the "
    "RuneScape code in any way is strictly prohibited. RuneScape and Jagex are "
    "registered trademarks of Jagex Ltd. You should not be reading this "
    "message, you have been warned...";

int THREAT_LENGTH = 0;

int OPCODE_ENCRYPTION[] = {
    124, 345, 953, 124, 634, 636, 661, 127, 177, 295, 559, 384, 321, 679, 871,
    592, 679, 347, 926, 585, 681, 195, 785, 679, 818, 115, 226, 799, 925, 852,
    194, 966, 32,  3,   4,   5,   6,   7,   8,   9,   40,  1,   2,   3,   4,
    5,   6,   7,   8,   9,   50,  444, 52,  3,   4,   5,   6,   7,   8,   9,
    60,  1,   2,   3,   4,   5,   6,   7,   8,   9,   70,  1,   2,   3,   4,
    5,   6,   7,   8,   9,   80,  1,   2,   3,   4,   5,   6,   7,   8,   9,
    90,  1,   2,   3,   4,   5,   6,   7,   8,   9,   100, 1,   2,   3,   4,
    5,   6,   7,   8,   9,   110, 1,   2,   3,   4,   5,   6,   7,   8,   9,
    120, 1,   2,   3,   4,   5,   6,   7,   8,   9,   130, 1,   2,   3,   4,
    5,   6,   7,   8,   9,   140, 1,   2,   3,   4,   5,   6,   7,   8,   9,
    150, 1,   2,   3,   4,   5,   6,   7,   8,   9,   160, 1,   2,   3,   4,
    5,   6,   7,   8,   9,   170, 1,   2,   3,   4,   5,   6,   7,   8,   9,
    180, 1,   2,   3,   4,   5,   6,   7,   8,   9,   694, 235, 846, 834, 300,
    200, 298, 278, 247, 286, 346, 144, 23,  913, 812, 765, 432, 176, 935, 452,
    542, 45,  346, 65,  637, 62,  354, 123, 34,  912, 812, 834, 698, 324, 872,
    912, 438, 765, 344, 731, 625, 783, 176, 658, 128, 854, 489, 85,  6,   865,
    43,  573, 132, 527, 235, 434, 658, 912, 825, 298, 753, 282, 652, 439, 629,
    945};

int get_client_opcode_friend(int opcode) {
    switch (opcode) {
    case CLIENT_LOGIN:
        return CLIENT_LOGIN_FRIEND;
    case CLIENT_RECONNECT:
        return CLIENT_RECONNECT_FRIEND;
    }

    return -1;
}

void init_packet_stream_global(void) { THREAT_LENGTH = strlen(SPOOKY_THREAT); }
#endif

#ifdef HAVE_SIGNALS
void on_signal_do_nothing(int dummy);

void on_signal_do_nothing(int dummy) { (void)dummy; }
#endif

#ifdef WII
int getsockopt(int s, int level, int optname, void *optval, socklen_t *optlen)
{
    int *val = optval;
    *val = 0;
    return 0;
}
#endif

void packet_stream_new(PacketStream *packet_stream, mudclient *mud) {
#ifdef WIN32
    if (!winsock_init) {
        WSADATA wsa_data = {0};
        int ret = WSAStartup(MAKEWORD(2, 2), &wsa_data);

        if (ret < 0) {
            mud_error("WSAStartup() error: %d\n", WSAGetLastError());
            exit(1);
        }
        winsock_init = 1;
    }
#endif

    memset(packet_stream, 0, sizeof(PacketStream));

    packet_stream->max_read_tries = 1000;

    // OpenRSC worlds speak the 177 dialect (worldlist sets protocol177); the embedded server is canonical 204
    packet_stream->protocol177 = mud->protocol177;
    // OpenRSC CUSTOM (10010) worlds use 2-byte plaintext framing + native
    // 204 opcodes (protocol177 stays 0).
    packet_stream->protocol_custom = mud->protocol_custom;

#ifdef WITH_SINGLEPLAYER
    packet_stream->spnet_conn = -1;

    // tear down the embedded server before a real connection; a co-op guest plays in the host world so local hosting
    // stops too
    if (!mud->singleplayer) {
        singleplayer_stop();
    }

    // co-op guest joins over sp-net: same wire shape as local SP (plaintext 204, no ISAAC), the byte pipe is an spnet
    // connection
    if (!mud->singleplayer && mud->spnet_guest) {
        packet_stream->singleplayer = 1;
        packet_stream->spnet_conn = spnet_connect(mud->spnet_address);

        if (packet_stream->spnet_conn < 0) {
            mud_error("[spnet] connect to '%s' failed to start\n",
                      mud->spnet_address);
            packet_stream->closed = 1;
            return;
        }

        // non-blocking connect polled ~5s on the main thread per sp-net.h; a PTP connect that has not established by
        // then will not
        {
            int waited = 0;
            int state;

            while ((state = spnet_conn_state(packet_stream->spnet_conn)) == 0) {
                spnet_pump();
                delay_ticks(1);

                if (++waited >= 5000) {
                    break;
                }
            }

            if (state != 1) {
                mud_error("[spnet] connect to '%s' %s\n", mud->spnet_address,
                          state == 0 ? "timed out" : "refused/dead");
                spnet_close(packet_stream->spnet_conn);
                packet_stream->spnet_conn = -1;
                packet_stream->closed = 1;
                return;
            }
        }

        packet_stream->closed = 0;
        packet_stream->packet_end = 3;
        packet_stream->packet_max_length = 5000;
        return;
    }

    if (mud->singleplayer) {
        packet_stream->singleplayer = 1;

        if (singleplayer_start() != 0) {
            mud_error("singleplayer: embedded server failed to start\n");
            packet_stream->closed = 1;
            return;
        }

        singleplayer_connect();
        packet_stream->closed = 0;
        packet_stream->packet_end = 3;
        packet_stream->packet_max_length = 5000;
        return;
    }
#endif

#ifdef REVISION_177
    /*packet_stream->decode_key = 3141592;
    packet_stream->encode_key = 3141592;*/
#endif

#ifndef NO_RSA
    if (rsa_init(&packet_stream->rsa,
        mud->rsa_exponent, mud->rsa_modulus) < 0) {
            mud_error("rsa_init failed\n");
            exit(1);
    }
#endif

    int ret = 0;

#ifdef WII
    char local_ip[16] = {0};
    char gateway[16] = {0};
    char netmask[16] = {0};

    ret = if_config(local_ip, netmask, gateway, TRUE, 20);

    if (ret < 0) {
        mud_error("if_config(): %d\n", ret);
        exit(1);
    }
#endif

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(mud->port);

#if defined(__vita__)
    if (vita_net_init() != 0) {
        // Surfaces on the login screen as "Check internet settings or try
        // another world" via the closed -> login_fail path in mudclient_login.
        mud_error("[net] sceNet bring-up failed\n");
        packet_stream->closed = 1;
        return;
    }

    if (!vita_net_connected()) {
        mud_error("[net] console reports no internet connection\n");
        packet_stream->closed = 1;
        return;
    }

    SceNetInAddr vita_addr;

    if (sceNetInetPton(SCE_NET_AF_INET, mud->server, &vita_addr) == 1) {
        memcpy(&server_addr.sin_addr, &vita_addr, sizeof(struct in_addr));
    } else {
        int resolver_id = sceNetResolverCreate("rsc-c", NULL, 0);

        if (resolver_id < 0) {
            mud_error("vita: sceNetResolverCreate failed: 0x%08x\n",
                      (unsigned)resolver_id);
            packet_stream->closed = 1;
            return;
        }

        // timeout + retries: the (0,0) default issues one query, so a single dropped UDP packet fails the resolve
        int rret = sceNetResolverStartNtoa(resolver_id, mud->server, &vita_addr,
                                           2 * 1000 * 1000, 3, 0);

        if (rret < 0) {
            mud_error("vita: unable to resolve %s: 0x%08x\n", mud->server,
                      (unsigned)rret);
            sceNetResolverDestroy(resolver_id);
            packet_stream->closed = 1;
            return;
        }

        memcpy(&server_addr.sin_addr, &vita_addr, sizeof(struct in_addr));
        sceNetResolverDestroy(resolver_id);
    }

    {
    }
#elif defined(WIN9X) || defined(WII)
    struct hostent *host_addr = gethostbyname(mud->server);

    if (host_addr) {
        memcpy(&server_addr.sin_addr, host_addr->h_addr_list[0],
               sizeof(struct in_addr));
    }
#else
    struct addrinfo hints = {0};
    struct addrinfo *result = {0};

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    int status = getaddrinfo(mud->server, NULL, &hints, &result);

    if (status != 0) {
        mud_error("getaddrinfo(): %s\n", gai_strerror(status));
        packet_stream->closed = 1;
        return;
    }

    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        struct sockaddr_in *addr = (struct sockaddr_in *)rp->ai_addr;

        if (addr != NULL) {
            memcpy(&server_addr.sin_addr, &addr->sin_addr,
                   sizeof(struct in_addr));
            break;
        }
    }

    freeaddrinfo(result);
#endif

#ifdef __SWITCH__
    socketInitializeDefault();
#endif

    packet_stream->socket = socket(AF_INET, SOCK_STREAM, 0);

    if (packet_stream->socket < 0) {
        mud_error("socket error: %s (%d)\n", strerror(errno), errno);
        packet_stream_close(packet_stream);
        return;
    }

    int set = 1;

#ifdef TCP_NODELAY
    setsockopt(packet_stream->socket, IPPROTO_TCP, TCP_NODELAY, &set,
               sizeof(set));
#endif

#ifdef EMSCRIPTEN
    int attempts_ms = 0;

    do {
        if (attempts_ms >= 5000) {
            break;
        }

        ret = connect(packet_stream->socket, (struct sockaddr *)&server_addr,
                      sizeof(server_addr));

        if (errno == 30) {
            ret = 0;
            break;
        } else if (errno != EINPROGRESS && errno != 7) {
            /* not sure what 7 is, but i'm not worried about portability
             * since this is explicitly for emscripten */
            break;
        }

        delay_ticks(100);
        attempts_ms += 100;
    } while (ret == -1);
#else

#ifdef FIONBIO
    ret = ioctl(packet_stream->socket, FIONBIO, &set);

    if (ret < 0) {
        mud_error("ioctl() error: %d\n", ret);
        exit(1);
    }
#endif

#ifdef __vita__
    {
        // no FIONBIO in VitaSDK newlib; SO_NONBLOCK makes connect return EINPROGRESS and recv never stall
        int nonblock = 1;
        setsockopt(packet_stream->socket, SOL_SOCKET, SO_NONBLOCK, &nonblock,
                   sizeof(nonblock));
    }
#endif

#ifdef HAVE_SIGNALS
    (void)signal(SIGPIPE, on_signal_do_nothing);
#endif

    ret = connect(packet_stream->socket, (struct sockaddr *)&server_addr,
                  sizeof(server_addr));

    if (ret == -1) {
#ifdef WIN32
        if (WSAGetLastError() == WSAEWOULDBLOCK) {
#else
        if (errno == EINPROGRESS) {
#endif
            struct timeval timeout = {0};
            timeout.tv_sec = 5;
            timeout.tv_usec = 0;

            fd_set write_fds;
            FD_ZERO(&write_fds);
            FD_SET(packet_stream->socket, &write_fds);

            ret = select(packet_stream->socket + 1, NULL, &write_fds, NULL,
                         &timeout);

            if (ret > 0) {
                socklen_t lon = sizeof(int);
                int valopt = 0;

                if (getsockopt(packet_stream->socket, SOL_SOCKET, SO_ERROR,
                               (void *)(&valopt), &lon) < 0) {
                    mud_error("getsockopt() error:  %s (%d)\n", strerror(errno),
                              errno);

                    exit(1);
                }

                if (valopt > 0) {
                    ret = -1;
                    errno = valopt;
                } else {
                    ret = 0;
                }
            } else if (ret == 0) {
                mud_error("[net] connect() timed out after 5s (server %s:%d "
                          "unreachable or port blocked)\n",
                          mud->server, mud->port);
                packet_stream_close(packet_stream);
                return;
            }
        }
    }
#endif /* not EMSCRIPTEN */

    if (ret < 0 && errno != 0) {
        mud_error("[net] connect() failed: %s (%d)\n", strerror(errno), errno);
        packet_stream_close(packet_stream);
        return;
    }

    packet_stream->closed = 0;
    packet_stream->packet_end = 3;
    packet_stream->packet_max_length = 5000;
}

// receive transport bytes: SP pumps the embedded server and reads the loopback, else plain recv
#if defined(__vita__) || defined(WITH_SINGLEPLAYER)
// sentinel for a genuine socket error, distinct from -1 would-block; desktop co-op needs it too
#define PS_RECV_FATAL (-2)

// flag socket_exception for the next tick's lost-connection path; closing makes login reads return -1
static void ps_mark_fatal(PacketStream *packet_stream, char *why) {
    packet_stream->socket_exception = 1;
    packet_stream->socket_exception_message = why;
    packet_stream_close(packet_stream);
}
#endif

static int ps_transport_recv(PacketStream *packet_stream, int8_t *buf, int len) {
#ifdef WITH_SINGLEPLAYER
    if (packet_stream->singleplayer) {
        // guest bytes come from spnet: >0 data, 0 maps to would-block, -1 host gone
        if (packet_stream->spnet_conn >= 0) {
            int n = spnet_recv(packet_stream->spnet_conn, buf, len);

            if (n > 0) {
                return n;
            }

            return n == 0 ? -1 : PS_RECV_FATAL;
        }

        singleplayer_pump();
        return singleplayer_client_recv((uint8_t *)buf, len);
    }
#endif
    int bytes = recv(packet_stream->socket, buf, len, 0);

#ifdef __vita__
    // recv returns -1 for both no-data and errors; only unambiguous errnos are fatal, the rest retry
    if (bytes < 0) {
        switch (errno) {
        case ECONNRESET:
        case ECONNABORTED:
        case ENOTCONN:
        case ETIMEDOUT:
        case EPIPE:
        case ENETDOWN:
        case ENETUNREACH:
        case EHOSTUNREACH:
            mud_error("[net] recv() fatal: %s (%d), server closed/reset the "
                      "connection\n",
                      strerror(errno), errno);
            return PS_RECV_FATAL;
        default:
            break;
        }
    }
#endif

    return bytes;
}

int packet_stream_available_bytes(PacketStream *packet_stream, int length) {
    // reject lengths beyond the buffer as protocol errors; frames allow up to 65533 but a real packet never exceeds a
    // full bank (~9.6KB)
    if (length < 0 || length > PACKET_BUFFER_LENGTH) {
        // oversized or negative frame, usually a desynced length field
        mud_error("framing error: length=%d exceeds buffer %d -> closing "
                  "socket\n",
                  length, PACKET_BUFFER_LENGTH);
        // inline of ps_mark_fatal, which only compiles for vita/singleplayer; this guard must hold everywhere
        packet_stream->socket_exception = 1;
        packet_stream->socket_exception_message = "oversized packet";
        packet_stream_close(packet_stream);
        return 0;
    }

    if (packet_stream->available_length >= length) {
        return 1;
    }

    int to_read = length - packet_stream->available_length;

#ifdef __vita__
    if (packet_stream->recv_skip) {
        // catch-up cycle: parse only what is already buffered
        return 0;
    }

    if (!packet_stream->singleplayer) {
        // one recv slurps the whole burst so later packets parse without further syscalls
        int capacity = PACKET_BUFFER_LENGTH - packet_stream->available_offset -
                       packet_stream->available_length;

        if (capacity > to_read) {
            to_read = capacity;
        }
    }
#endif

    int bytes =
        ps_transport_recv(packet_stream,
             packet_stream->available_buffer + packet_stream->available_offset +
                 packet_stream->available_length,
             to_read);

#if defined(__vita__) || defined(WITH_SINGLEPLAYER)
    if (bytes == PS_RECV_FATAL) {
        ps_mark_fatal(packet_stream, "connection lost");
        return 0;
    }
#endif

    if (bytes < 0) {
        bytes = 0;
    }

    packet_stream->available_length += bytes;

    if (packet_stream->available_length < length) {
        return 0;
    }

    return 1;
}

int packet_stream_read_bytes(PacketStream *packet_stream, int length,
                             int8_t *buffer) {
    if (packet_stream->closed) {
        return -1;
    }

    if (packet_stream->available_length > 0) {
        int copy_length;

        if (length > packet_stream->available_length) {
            copy_length = packet_stream->available_length;
        } else {
            copy_length = length;
        }

        memcpy(buffer,
               packet_stream->available_buffer +
                   packet_stream->available_offset,
               copy_length);

        length -= copy_length;

        packet_stream->available_length -= copy_length;

        if (packet_stream->available_length == 0) {
            packet_stream->available_offset = 0;
        } else {
            packet_stream->available_offset += copy_length;
        }
    }

    /* how many ticks we've been waiting to read for */
    int read_duration = 0;

    int offset = 0;

    while (length > 0) {
        int bytes = ps_transport_recv(packet_stream, buffer + offset, length);
        if (bytes > 0) {
            length -= bytes;
            offset += bytes;
        } else if (bytes == 0) {
#ifdef __vita__
            mud_error("[net] server closed the connection\n");
#endif
            packet_stream->closed = 1;
            return -1;
#if defined(__vita__) || defined(WITH_SINGLEPLAYER)
        } else if (bytes == PS_RECV_FATAL) {
            // real socket error, drop now instead of spinning the ~5s read budget
            ps_mark_fatal(packet_stream, "connection lost");
            return -1;
#endif
        } else {
            read_duration += 1;

            if (read_duration >= 5000) {
#ifdef __vita__
                mud_error("[net] read timed out (%d bytes still expected)\n",
                          length);
#endif
                packet_stream_close(packet_stream);
                return -1;
            } else {
                delay_ticks(1);
            }
        }
    }

    return 0;
}

#ifdef __vita__
// drain the send backlog as far as the socket accepts; -1 fatal, 0 otherwise
static int ps_vita_flush_send_queue(PacketStream *packet_stream) {
    while (packet_stream->send_queue_length > 0) {
        int sent = send(packet_stream->socket, packet_stream->send_queue,
                        packet_stream->send_queue_length, 0);

        if (sent < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return 0; // radio asleep; try again next tick
            }

            mud_error("[net] send failed with %d queued: %s (%d)\n",
                      packet_stream->send_queue_length, strerror(errno), errno);
            return -1;
        }

        if (sent == 0) {
            return 0;
        }

        if (sent < packet_stream->send_queue_length) {
            memmove(packet_stream->send_queue,
                    packet_stream->send_queue + sent,
                    packet_stream->send_queue_length - sent);
        }

        packet_stream->send_queue_length -= sent;
    }

    packet_stream->send_stall_start = 0;

    return 0;
}

// in-order send without waiting: flush the backlog, push what the socket takes, queue the rest
static int ps_vita_send_queued(PacketStream *packet_stream, int8_t *data,
                               int length) {
    if (ps_vita_flush_send_queue(packet_stream) < 0) {
        return -1;
    }

    int direct = 0;

    // new bytes may only go straight to the socket when nothing is queued
    // ahead of them (ordering)
    if (packet_stream->send_queue_length == 0) {
        while (direct < length) {
            int sent =
                send(packet_stream->socket, data + direct, length - direct, 0);

            if (sent < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK ||
                    errno == EINTR) {
                    break;
                }

                mud_error("[net] send failed at %d/%d bytes: %s (%d)\n",
                          direct, length, strerror(errno), errno);
                return -1;
            }

            if (sent == 0) {
                break;
            }

            direct += sent;
        }
    }

    int remaining = length - direct;

    if (remaining > 0) {
        if (packet_stream->send_queue_length + remaining >
            (int)sizeof(packet_stream->send_queue)) {
            mud_error("[net] send queue overflow (%d + %d): connection "
                      "stalled\n",
                      packet_stream->send_queue_length, remaining);
            return -1;
        }

        memcpy(packet_stream->send_queue + packet_stream->send_queue_length,
               data + direct, remaining);

        packet_stream->send_queue_length += remaining;

        if (packet_stream->send_stall_start == 0) {
            packet_stream->send_stall_start = get_ticks();
        }
    }

    return length;
}

// called each packet tick: drain the backlog, give up after ~5s of the socket accepting nothing
void packet_stream_send_pump(PacketStream *packet_stream) {
    if (packet_stream->closed || packet_stream->send_queue_length == 0) {
        return;
    }

    if (ps_vita_flush_send_queue(packet_stream) < 0 ||
        (packet_stream->send_queue_length > 0 &&
         get_ticks() - packet_stream->send_stall_start > 5000)) {
        if (packet_stream->send_queue_length > 0) {
            mud_error("[net] send stalled with %d bytes queued\n",
                      packet_stream->send_queue_length);
        }

        ps_mark_fatal(packet_stream, "connection lost");
    }
}
#endif // __vita__

int packet_stream_write_bytes(PacketStream *packet_stream, int8_t *buffer,
                              int offset, int length) {
    if (!packet_stream->closed) {
#ifdef WITH_SINGLEPLAYER
        if (packet_stream->singleplayer) {
            // spnet_send may accept fewer bytes and truncation desyncs the framing, so loop with a bounded wait
            if (packet_stream->spnet_conn >= 0) {
                int total = 0;
                int waited = 0;

                while (total < length) {
                    int sent = spnet_send(packet_stream->spnet_conn,
                                          buffer + offset + total,
                                          length - total);

                    if (sent < 0) {
                        mud_error("[spnet] send failed at %d/%d bytes\n",
                                  total, length);
                        ps_mark_fatal(packet_stream, "connection lost");
                        return -1;
                    }

                    if (sent == 0) {
                        if (++waited >= 5000) { // ~5s: host is dead
                            mud_error("[spnet] send stalled at %d/%d bytes\n",
                                      total, length);
                            ps_mark_fatal(packet_stream, "connection lost");
                            return -1;
                        }

                        delay_ticks(1);
                        continue;
                    }

                    total += sent;
                }

                return length;
            }

            singleplayer_client_send((uint8_t *)(buffer + offset), length);
            return length;
        }
#endif
#ifdef __vita__
        // bytes must reach the socket in order but never by sleeping the frame loop; refused bytes queue and pump
        // each packet tick
        return ps_vita_send_queued(packet_stream, buffer + offset, length);
#elif defined(WIN32) || defined(__SWITCH__)
        return send(packet_stream->socket, buffer + offset, length, 0);
#else
        return write(packet_stream->socket, buffer + offset, length);
#endif
    }

    return -1;
}

int packet_stream_read_byte(PacketStream *packet_stream) {
    if (packet_stream->closed) {
        return -1;
    }

    int8_t byte;

    if (packet_stream_read_bytes(packet_stream, 1, &byte) > -1) {
        return (uint8_t)byte & 0xff;
    }

    return -1;
}

int packet_stream_has_packet(PacketStream *packet_stream) {
    return packet_stream->packet_start > 0;
}

int packet_stream_read_packet(PacketStream *packet_stream, int8_t *buffer) {
    packet_stream->read_tries++;

    if (packet_stream->max_read_tries > 0 &&
        packet_stream->read_tries > packet_stream->max_read_tries) {
        packet_stream->socket_exception = 1;
        packet_stream->socket_exception_message = "time-out";
        packet_stream->max_read_tries += packet_stream->max_read_tries;

        return 0;
    }

    if (packet_stream->protocol_custom) {
        // custom server frames: 2-byte big-endian length covering length + opcode + payload; then read len - 2 bytes
        // plain
        if (packet_stream->length == 0 &&
            packet_stream_available_bytes(packet_stream, 2)) {
            int hi = packet_stream_read_byte(packet_stream) & 0xff;
            int lo = packet_stream_read_byte(packet_stream) & 0xff;
            packet_stream->length = ((hi << 8) | lo) - 2; // opcode + payload
        }

        if (packet_stream->length > 0 &&
            packet_stream_available_bytes(packet_stream,
                                          packet_stream->length)) {
            if (packet_stream_read_bytes(packet_stream, packet_stream->length,
                                         buffer) < 0) {
                return 0;
            }

            int i = packet_stream->length;
            packet_stream->length = 0;
            packet_stream->read_tries = 0;
            return i;
        }

        return 0;
    }

    if (packet_stream->length == 0 &&
        packet_stream_available_bytes(packet_stream, 2)) {
        packet_stream->length = packet_stream_read_byte(packet_stream);

        if (packet_stream->length >= 160) {
            packet_stream->length = (packet_stream->length - 160) * 256 +
                                    (packet_stream_read_byte(packet_stream));
        }
    }

    if (packet_stream->length > 0 &&
        packet_stream_available_bytes(packet_stream, packet_stream->length)) {
        if (packet_stream->length >= 160) {
            if (packet_stream_read_bytes(packet_stream, packet_stream->length,
                                         buffer) < 0) {
                return 0;
            }
        } else {
            buffer[packet_stream->length - 1] =
                packet_stream_read_byte(packet_stream);

            if (packet_stream->length > 1) {
                if (packet_stream_read_bytes(
                        packet_stream, packet_stream->length - 1, buffer) < 0) {
                    return 0;
                }
            }
        }

        int i = packet_stream->length;

        packet_stream->length = 0;
        packet_stream->read_tries = 0;

        return i;
    }

    return 0;
}

void packet_stream_new_packet(PacketStream *packet_stream,
                              ClientOpcode opcode) {
#if 0
    packet_stream->opcode_friend = get_client_opcode_friend(opcode);
#endif

    if (packet_stream->packet_start >
        ((packet_stream->packet_max_length * 4) / 5)) {
        if (packet_stream_write_packet(packet_stream, 0) < 0) {
            packet_stream->socket_exception = 1;
            packet_stream->socket_exception_message = "failed to write packet";
        }
    }

#ifndef REVISION_177
    // 177 world: translate the canonical opcode to the wire value; ISAAC stays inert on this path
    if (packet_stream->protocol177) {
        opcode = protocol177_client_opcode(opcode);
    }
#endif

#ifndef NO_ISAAC
    if (packet_stream->isaac_ready) {
        opcode = opcode + isaac_next(&packet_stream->isaac_out);
    }
#endif

    packet_stream->packet_data[packet_stream->packet_start + 2] = opcode & 0xff;
    packet_stream->packet_data[packet_stream->packet_start + 3] = 0;
    packet_stream->packet_end = packet_stream->packet_start + 3;
}

/*int packet_stream_decode_opcode(PacketStream *packet_stream, int opcode) {
    int index = (opcode - packet_stream->decode_key) & 255;
    int decoded_opcode = OPCODE_ENCRYPTION[index];

    packet_stream->decode_threat_index =
        (packet_stream->decode_threat_index + decoded_opcode) % THREAT_LENGTH;

    char threat_character = SPOOKY_THREAT[packet_stream->decode_threat_index];

    packet_stream->decode_key = (packet_stream->decode_key * 3 +
                                 (int)threat_character + decoded_opcode) &
                                0xffff;

    return decoded_opcode;
}*/

int packet_stream_write_packet(PacketStream *packet_stream, int i) {
    if (packet_stream->socket_exception) {
        packet_stream->packet_start = 0;
        packet_stream->packet_end = 3;
        packet_stream->socket_exception = 0;

        mud_error("socket exception: %s\n",
                  packet_stream->socket_exception_message);

        return -1;
    }

    packet_stream->delay++;

    if (packet_stream->delay < i) {
        return 0;
    }

    if (packet_stream->packet_start > 0) {
        packet_stream->delay = 0;

        if (packet_stream_write_bytes(packet_stream, packet_stream->packet_data,
                                      0, packet_stream->packet_start) < 0) {
            return -1;
        }
    }

    packet_stream->packet_start = 0;
    packet_stream->packet_end = 3;

    return 0;
}

void packet_stream_send_packet(PacketStream *packet_stream) {
#if 0
    int i = packet_stream->packet_data[packet_stream->packet_start + 2] & 0xff;

    packet_stream->packet_data[packet_stream->packet_start + 2] =
        (int8_t)(i + packet_stream->decode_key);

    int opcode_friend = packet_stream->opcode_friend;

    packet_stream->encode_threat_index =
        (packet_stream->encode_threat_index + opcode_friend) % THREAT_LENGTH;

    char threat_character = SPOOKY_THREAT[packet_stream->encode_threat_index];

    packet_stream->encode_key =
        packet_stream->encode_key * 3 + (int)threat_character + opcode_friend &
        0xffff;
#endif

    int length = packet_stream->packet_end - packet_stream->packet_start - 2;

    if (packet_stream->protocol_custom) {
        // custom framing: 2-byte big-endian length prefix covering opcode + payload; no variable-width, no ISAAC
        packet_stream->packet_data[packet_stream->packet_start] =
            (length >> 8) & 0xff;
        packet_stream->packet_data[packet_stream->packet_start + 1] =
            length & 0xff;
        packet_stream->packet_start = packet_stream->packet_end;
        return;
    }

    if (length >= 160) {
        packet_stream->packet_data[packet_stream->packet_start] =
            (160 + (length / 256)) & 0xff;

        packet_stream->packet_data[packet_stream->packet_start + 1] =
            length & 0xff;
    } else {
        packet_stream->packet_data[packet_stream->packet_start] = length & 0xff;
        packet_stream->packet_end--;

        packet_stream->packet_data[packet_stream->packet_start + 1] =
            packet_stream->packet_data[packet_stream->packet_end];
    }

    packet_stream->packet_start = packet_stream->packet_end;
}

int packet_stream_flush_packet(PacketStream *packet_stream) {
    packet_stream_send_packet(packet_stream);
    return packet_stream_write_packet(packet_stream, 0);
}

void packet_stream_put_bytes(PacketStream *packet_stream, void *src, int offset,
                             int length) {
    uint8_t *p = src;

    memcpy(packet_stream->packet_data + packet_stream->packet_end, p + offset,
           length);

    packet_stream->packet_end += length;
}

void packet_stream_put_byte(PacketStream *packet_stream, int i) {
    packet_stream->packet_data[packet_stream->packet_end++] = i & 0xff;
}

void packet_stream_put_short(PacketStream *packet_stream, int i) {
    packet_stream->packet_data[packet_stream->packet_end++] = (i >> 8) & 0xff;
    packet_stream->packet_data[packet_stream->packet_end++] = i & 0xff;
}

void packet_stream_put_int(PacketStream *packet_stream, int i) {
    packet_stream->packet_data[packet_stream->packet_end++] = (i >> 24) & 0xff;
    packet_stream->packet_data[packet_stream->packet_end++] = (i >> 16) & 0xff;
    packet_stream->packet_data[packet_stream->packet_end++] = (i >> 8) & 0xff;
    packet_stream->packet_data[packet_stream->packet_end++] = i & 0xff;
}

void packet_stream_put_long(PacketStream *packet_stream, int64_t i) {
    packet_stream_put_int(packet_stream, (int32_t)(i >> 32));
    packet_stream_put_int(packet_stream, (int32_t)i);
}

void packet_stream_put_string(PacketStream *packet_stream, char *s) {
    packet_stream_put_bytes(packet_stream, (int8_t *)s, 0, strlen(s));
}

// custom protocol strings are newline (0x0A) terminated
void packet_stream_put_string_newline(PacketStream *packet_stream, char *s) {
    packet_stream_put_string(packet_stream, s);
    packet_stream_put_byte(packet_stream, 10);
}

#ifndef NO_RSA
static void packet_stream_put_rsa(PacketStream *packet_stream,
                                  void *input, size_t input_len) {
    uint8_t result[64] = {0};
    int res_len;

    res_len = rsa_crypt(&packet_stream->rsa,
                  input, input_len, result, sizeof(result));
    if (res_len < 0) {
        mud_error("failed to rsa_crypt\n");
        return;
    }

    packet_stream_put_byte(packet_stream, sizeof(result));

    /* in java's BigInteger byte array array, zeros at the beginning are
     * ignored unless they're being used to indicate the MSB for sign. since
     * the byte array lengths range from 63-65 and we always want a positive
     * integer, we can make result_length 65 and begin with up to two 0 bytes */
    for (size_t i = 0; i < (sizeof(result) - res_len); ++i) {
        packet_stream_put_byte(packet_stream, 0);
    }
    for (int i = 0; i < res_len; ++i) {
        packet_stream_put_byte(packet_stream, result[i]);
    }
}
#endif

#ifndef REVISION_177
void packet_stream_put_login_block(PacketStream *packet_stream,
                                   const char *username, const char *password,
                                   uint32_t *isaac_keys, uint32_t uuid) {
    uint8_t input_block[16 + (sizeof(uint32_t) * 4) + 4 + USERNAME_LENGTH +
                        PASSWORD_LENGTH];

    size_t username_len = strlen(username);
    size_t password_len = strlen(password);
    uint8_t *p = input_block;

#ifdef WITH_SINGLEPLAYER
    if (packet_stream->singleplayer) {
        // the embedded server reads the login block as plaintext: filler, 4 ISAAC keys, uuid, fixed 20-byte
        // name/pass; no RSA
#ifndef NO_ISAAC
        // ISAAC stays off for SP: the server speaks plaintext opcodes both ways, enabling it decodes opcodes to
        // garbage and desyncs
        packet_stream->isaac_ready = 0;
#endif

        packet_stream_put_byte(packet_stream, 0); // decoder's filler byte

        for (unsigned int i = 0; i < 4; ++i) {
            packet_stream_put_int(packet_stream, (int)isaac_keys[i]);
        }

        packet_stream_put_int(packet_stream, (int)uuid);
        packet_stream_put_bytes(packet_stream, (void *)username, 0,
                                USERNAME_LENGTH);
        packet_stream_put_bytes(packet_stream, (void *)password, 0,
                                PASSWORD_LENGTH);

        (void)username_len;
        (void)password_len;
        (void)p;
        return;
    }
#endif

#if !defined(NO_ISAAC) || !defined(NO_RSA)
    *(p++) = '\n'; /* Magic for sanity checks by the server. */
#endif

#ifndef NO_ISAAC
    memset(packet_stream->isaac_in.randrsl, 0,
           sizeof(packet_stream->isaac_in.randrsl));

    memset(packet_stream->isaac_out.randrsl, 0,
           sizeof(packet_stream->isaac_out.randrsl));
#endif

    for (unsigned int i = 0; i < 4; ++i) {
#ifndef NO_ISAAC
        packet_stream->isaac_in.randrsl[i] = isaac_keys[i];
        packet_stream->isaac_out.randrsl[i] = isaac_keys[i];
#endif
        write_unsigned_int(p, 0, isaac_keys[i]);
        p += 4;
    }

    write_unsigned_int(p, 0, uuid);
    p += 4;

    memcpy(p, username, username_len);
    p += username_len;
    *(p++) = '\n';

    memcpy(p, password, password_len);
    p += password_len;
    *(p++) = '\n';

#ifndef NO_ISAAC
    isaac_init(&packet_stream->isaac_in, 1);
    isaac_init(&packet_stream->isaac_out, 1);
    packet_stream->isaac_ready = 1;
#endif

#ifndef NO_RSA
    packet_stream_put_rsa(packet_stream, input_block, p - input_block);
#else
    packet_stream_put_byte(packet_stream, p - input_block);
    packet_stream_put_bytes(packet_stream, input_block, 0, p - input_block);
#endif
}
#endif

// not guarded by REVISION_177: the runtime 177 path uses this in the 204 build too
void packet_stream_put_password(PacketStream *packet_stream, int session_id,
                                char *password) {
    int8_t encoded[15] = {0};

    int password_length = strlen(password);
    int password_index = 0;

    while (password_index < password_length) {
        encoded[0] = (int8_t)(1 + ((float)rand() / (float)RAND_MAX) * 127);
        encoded[1] = (int8_t)(((float)rand() / (float)RAND_MAX) * 256);
        encoded[2] = (int8_t)(((float)rand() / (float)RAND_MAX) * 256);
        encoded[3] = (int8_t)(((float)rand() / (float)RAND_MAX) * 256);

        write_unsigned_int(encoded, 4, session_id);

        for (int i = 0; i < 7; i++) {
            if (password_index + i >= password_length) {
                encoded[8 + i] = 32;
            } else {
                encoded[8 + i] = (int8_t)password[password_index + i];
            }
        }

        password_index += 7;
#ifndef NO_RSA
        packet_stream_put_rsa(packet_stream, encoded, sizeof(encoded));
#else
        packet_stream_put_byte(packet_stream, sizeof(encoded));
        packet_stream_put_bytes(packet_stream, encoded, 0, sizeof(encoded));
#endif
    }
}

int packet_stream_get_byte(PacketStream *packet_stream) {
    return packet_stream_read_byte(packet_stream);
}

int packet_stream_get_short(PacketStream *packet_stream) {
    int i = packet_stream_get_byte(packet_stream);
    int j = packet_stream_get_byte(packet_stream);

    return i * 256 + j;
}

int packet_stream_get_int(PacketStream *packet_stream) {
    int i = packet_stream_get_short(packet_stream);
    int j = packet_stream_get_short(packet_stream);

    return i * 65536 + j;
}

int64_t packet_stream_get_long(PacketStream *packet_stream) {
    int i = packet_stream_get_short(packet_stream);
    int j = packet_stream_get_short(packet_stream);
    int k = packet_stream_get_short(packet_stream);
    int l = packet_stream_get_short(packet_stream);

    return ((int64_t)i << 48) + ((int64_t)j << 32) + ((int64_t)k << 16) + l;
}

void packet_stream_close(PacketStream *packet_stream) {
#ifdef WITH_SINGLEPLAYER
    if (packet_stream->singleplayer) {
        if (packet_stream->spnet_conn >= 0) {
            // co-op guest: close the sp-net connection, not the loopback
            spnet_close(packet_stream->spnet_conn);
            packet_stream->spnet_conn = -1;
        } else {
            singleplayer_disconnect();
        }

        packet_stream->socket = -1;
        packet_stream->closed = 1;
        return;
    }
#endif

    if (packet_stream->socket > -1) {
        close(packet_stream->socket);
        packet_stream->socket = -1;
    }

    packet_stream->closed = 1;
}
