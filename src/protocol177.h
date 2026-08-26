#ifndef _H_PROTOCOL177
#define _H_PROTOCOL177

// translates opcodes between the canonical 204 protocol and the revision-177 dialect at the packet boundary

#define PROTOCOL177_VERSION 177

// 177's dedicated reconnect opcode, passed through unchanged (no 204 name)
#define PROTOCOL177_CLIENT_RECONNECT 19

// canonical (204) client opcode to 177 wire value; unknown values pass through unchanged
int protocol177_client_opcode(int opcode);

// 177 wire server opcode to canonical (204) value; -1 if no 204 equivalent
int protocol177_server_opcode(int opcode);

#endif
