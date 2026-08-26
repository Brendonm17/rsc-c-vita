#ifndef _H_PACKET_STREAM
#define _H_PACKET_STREAM

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include <errno.h>
#include <fcntl.h>

#ifdef WII
#include <network.h>
#else
#ifdef WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#ifndef __vita__
#include <sys/ioctl.h> // VitaSDK newlib lacks it; FIONBIO only
#endif
#include <sys/socket.h>
#include <sys/types.h>
#endif
#endif

#ifdef REVISION_177
#define NO_ISAAC
#endif

#ifndef NO_ISAAC
#include "lib/isaac.h"
#endif

#ifndef NO_RSA
#include "lib/rsa/rsa.h"
#endif

#if !defined(WIN32) && !defined(WII)
#define HAVE_SIGNALS
#endif

#define USERNAME_LENGTH 20
#define PASSWORD_LENGTH 20

// sized to the reference client's 30000 buffer; a full custom bank is ~9.6KB and resends whole after every update.
// 5000 overflowed
#define PACKET_BUFFER_LENGTH 30000

/*extern char *SPOOKY_THREAT;
extern int THREAT_LENGTH;

extern int OPCODE_ENCRYPTION[];

int get_client_opcode_friend(int opcode);*/

typedef struct PacketStream PacketStream;

#include "mudclient.h"
#include "utility.h"

#ifdef REVISION_177
void init_packet_stream_global(void);
#endif

struct PacketStream {
    int socket;
    int closed;
    int delay;
    int length;
    int max_read_tries;
    int8_t packet_data[PACKET_BUFFER_LENGTH];
    int packet_end;
    int packet_max_length;
    int packet_start;
    int read_tries;
    int socket_exception;
    char *socket_exception_message;
    int8_t available_buffer[PACKET_BUFFER_LENGTH];
    int available_length;
    int available_offset;

#ifdef __vita__
    // send queue for bytes the socket refuses (Wi-Fi power-save); pumped each packet tick instead of sleeping in send
    uint8_t send_queue[16384];
    int send_queue_length;
    uint64_t send_stall_start;

    // 1 = catch-up tick, no recv syscalls; only already-buffered packets parse
    int recv_skip;
#endif

#ifdef WITH_SINGLEPLAYER
    int singleplayer; // plaintext-204 wire, SP and co-op guests
    int spnet_conn; // -1 unless a co-op guest conn handle
#endif

    // translate opcodes to/from the revision-177 dialect (see protocol177.h)
    int protocol177;

    // custom framing: 2-byte big-endian length, plaintext opcodes, no ISAAC; client->server len = 1+payload,
    // server->client 3+payload
    int protocol_custom;

#ifndef NO_RSA
    struct rsa rsa;
#endif

#ifndef NO_ISAAC
    struct isaac isaac_in;
    struct isaac isaac_out;
    int isaac_ready;
#endif

#ifdef REVISION_177
    /*int decode_key;
    int decode_threat_index;

    int encode_key;
    int encode_threat_index;

    int opcode_friend;*/
#endif
};

void packet_stream_new(PacketStream *packet_stream, mudclient *mud);
int packet_stream_available_bytes(PacketStream *packet_stream, int length);
int packet_stream_read_bytes(PacketStream *packet_stream, int length,
                             int8_t *buffer);
int packet_stream_write_bytes(PacketStream *packet_stream, int8_t *buffer,
                              int offset, int length);
int packet_stream_read_byte(PacketStream *packet_stream);
int packet_stream_has_packet(PacketStream *packet_stream);
int packet_stream_read_packet(PacketStream *packet_stream, int8_t *buffer);
void packet_stream_new_packet(PacketStream *packet_stream, ClientOpcode opcode);
/*int packet_stream_decode_opcode(PacketStream *packet_stream, int opcode);*/
int packet_stream_write_packet(PacketStream *packet_stream, int i);
void packet_stream_send_packet(PacketStream *packet_stream);
#ifdef __vita__
void packet_stream_send_pump(PacketStream *packet_stream);
#endif
void packet_stream_put_bytes(PacketStream *packet_stream, void *src, int offset,
                             int length);
void packet_stream_put_byte(PacketStream *packet_stream, int i);
void packet_stream_put_short(PacketStream *packet_stream, int i);
void packet_stream_put_int(PacketStream *packet_stream, int i);
void packet_stream_put_long(PacketStream *packet_stream, int64_t i);
void packet_stream_put_string(PacketStream *packet_stream, char *s);
void packet_stream_put_string_newline(PacketStream *packet_stream, char *s);
// always compiled: the runtime 177 path needs put_password in the 204 build
void packet_stream_put_password(PacketStream *packet_stream, int session_id,
                                char *password);
#ifndef REVISION_177
void packet_stream_put_login_block(PacketStream *packet_stream,
                                   const char *username, const char *password,
                                   uint32_t *isaac_keys, uint32_t uuid);
#endif
int packet_stream_get_byte(PacketStream *packet_stream);
int packet_stream_get_short(PacketStream *packet_stream);
int packet_stream_get_int(PacketStream *packet_stream);
int64_t packet_stream_get_long(PacketStream *packet_stream);
int packet_stream_flush_packet(PacketStream *packet_stream);
void packet_stream_close(PacketStream *packet_stream);

#endif
