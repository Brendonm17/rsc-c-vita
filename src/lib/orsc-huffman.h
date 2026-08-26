#ifndef _H_ORSC_HUFFMAN
#define _H_ORSC_HUFFMAN

#include <stdint.h>

// OpenRSC custom-protocol (10010) chat codec: canonical Huffman stream behind a smart length

// max characters encoded/decoded in one message
#define ORSC_HUFFMAN_MAX_CHARS 256

// build the canonical-huffman tables; idempotent
void orsc_huffman_init(void);

// encode length characters of message msb-first, returns byte count or -1 if it won't fit
int orsc_huffman_encode(const char *message, int length, uint8_t *dest,
                        int dest_size);

// decode char_count characters from src into dest, returns characters written
int orsc_huffman_decode(const uint8_t *src, int src_size, int char_count,
                        char *dest, int dest_size);

// smart08_16: < 128 is one byte, otherwise a big-endian short + 32768.
int orsc_smart_length_size(int value);
void orsc_smart_length_put(uint8_t *dest, int value);
// reads the smart length at *offset, advancing it; returns the value
int orsc_smart_length_get(const int8_t *src, int size, int *offset);

#endif
