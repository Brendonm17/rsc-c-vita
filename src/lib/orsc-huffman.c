#include "orsc-huffman.h"

#include <string.h>

// bit length each byte value 0..255 encodes to
static const uint8_t orsc_huffman_bits[256] = {
    22, 22, 22, 22, 22, 22, 21, 22, 22, 20, 22, 22, 22, 21, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    3,  8,  22, 16, 22, 16, 17, 7,  13, 13, 13, 16, 7,  10, 6,  16,
    10, 11, 12, 12, 12, 12, 13, 13, 14, 14, 11, 14, 19, 15, 17, 8,
    11, 9,  10, 10, 10, 10, 11, 10, 9,  7,  12, 11, 10, 10, 9,  10,
    10, 12, 10, 9,  8,  12, 12, 9,  14, 8,  12, 17, 16, 17, 22, 13,
    21, 4,  7,  6,  5,  3,  6,  6,  5,  4,  10, 7,  5,  6,  4,  4,
    6,  10, 5,  4,  4,  5,  7,  6,  10, 6,  10, 22, 19, 22, 14, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22,
    22, 22, 22, 22, 22, 22, 22, 21, 22, 21, 22, 22, 22, 21, 22, 22,
};

// code for each byte, left-aligned (bit 31 is the first bit on the wire)
static uint32_t cipher_block[256];

// Huffman walk tree: at node n, a 0 bit steps to n+1, a 1 bit steps to tree[n]
#define ORSC_HUFFMAN_TREE_MAX 2048
static int32_t cipher_tree[ORSC_HUFFMAN_TREE_MAX];
static int cipher_tree_size;

static int orsc_huffman_ready;

void orsc_huffman_init(void) {
    if (orsc_huffman_ready) {
        return;
    }

    memset(cipher_block, 0, sizeof(cipher_block));
    memset(cipher_tree, 0, sizeof(cipher_tree));
    cipher_tree_size = 8; // OpenRSC cipherDictionary starts at new int[8]

    int32_t block_builder[33] = {0};
    int next_free = 0;

    for (int value = 0; value < 256; value++) {
        int bits = orsc_huffman_bits[value];
        int32_t builder_bit = (int32_t)(1u << (32 - bits));
        int32_t builder = block_builder[bits];

        cipher_block[value] = (uint32_t)builder;

        int32_t assigned;

        if ((builder & builder_bit) == 0) {
            assigned = builder | builder_bit;

            for (int i = bits - 1; i > 0; i--) {
                int32_t other = block_builder[i];

                if (builder != other) {
                    break;
                }

                int32_t other_bit = (int32_t)(1u << (32 - i));

                if ((other & other_bit) == 0) {
                    block_builder[i] = other | other_bit;
                } else {
                    block_builder[i] = block_builder[i - 1];
                    break;
                }
            }
        } else {
            assigned = block_builder[bits - 1];
        }

        block_builder[bits] = assigned;

        for (int i = bits + 1; i <= 32; i++) {
            if (block_builder[i] == builder) {
                block_builder[i] = assigned;
            }
        }

        // walk the code's bits, minting tree nodes as needed
        int node = 0;

        for (int i = 0; i < bits; i++) {
            uint32_t selector = 0x80000000u >> i;

            if (((uint32_t)builder & selector) == 0) {
                node++;
            } else {
                if (cipher_tree[node] == 0) {
                    cipher_tree[node] = next_free;
                }

                node = cipher_tree[node];
            }

            if (node >= cipher_tree_size) {
                // clamp rather than run off the end since the table is fixed size
                cipher_tree_size *= 2;

                if (cipher_tree_size > ORSC_HUFFMAN_TREE_MAX) {
                    cipher_tree_size = ORSC_HUFFMAN_TREE_MAX;
                }

                if (node >= ORSC_HUFFMAN_TREE_MAX) {
                    orsc_huffman_ready = 1;
                    return;
                }
            }
        }

        cipher_tree[node] = ~value;

        if (node >= next_free) {
            next_free = node + 1;
        }
    }

    orsc_huffman_ready = 1;
}

int orsc_huffman_encode(const char *message, int length, uint8_t *dest,
                        int dest_size) {
    orsc_huffman_init();

    int bit = 0;

    memset(dest, 0, dest_size);

    for (int i = 0; i < length; i++) {
        // everything this client can type is ASCII and passes through unchanged
        int c = (uint8_t)message[i];
        uint32_t code = cipher_block[c];
        int bits = orsc_huffman_bits[c];

        if (((bit + bits + 7) >> 3) > dest_size) {
            return -1;
        }

        for (int b = 0; b < bits; b++) {
            if (code & (0x80000000u >> b)) {
                int at = bit + b;
                dest[at >> 3] |= (uint8_t)(0x80 >> (at & 7));
            }
        }

        bit += bits;
    }

    return (bit + 7) >> 3;
}

int orsc_huffman_decode(const uint8_t *src, int src_size, int char_count,
                        char *dest, int dest_size) {
    orsc_huffman_init();

    int node = 0;
    int out = 0;
    int total_bits = src_size * 8;

    for (int bit = 0; bit < total_bits && out < char_count &&
                      out < dest_size - 1;
         bit++) {
        int set = (src[bit >> 3] >> (7 - (bit & 7))) & 1;

        node = set ? cipher_tree[node] : node + 1;

        if (node < 0 || node >= ORSC_HUFFMAN_TREE_MAX) {
            break;
        }

        int32_t value = cipher_tree[node];

        if (value < 0) {
            dest[out++] = (char)(~value);
            node = 0;
        }
    }

    dest[out] = '\0';
    return out;
}

int orsc_smart_length_size(int value) { return value < 128 ? 1 : 2; }

void orsc_smart_length_put(uint8_t *dest, int value) {
    if (value < 128) {
        dest[0] = (uint8_t)value;
        return;
    }

    int encoded = value + 32768;

    dest[0] = (uint8_t)((encoded >> 8) & 0xff);
    dest[1] = (uint8_t)(encoded & 0xff);
}

int orsc_smart_length_get(const int8_t *src, int size, int *offset) {
    if (*offset >= size) {
        return 0;
    }

    int first = (uint8_t)src[*offset];

    if (first < 128) {
        (*offset)++;
        return first;
    }

    if (*offset + 1 >= size) {
        *offset = size;
        return 0;
    }

    int value = ((first << 8) | (uint8_t)src[*offset + 1]) - 32768;
    *offset += 2;

    return value;
}
