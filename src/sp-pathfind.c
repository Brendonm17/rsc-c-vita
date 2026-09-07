#include "sp-pathfind.h"

#include <stdlib.h>
#include <string.h>

// obstacle bitfield

static int grid_get(const sp_pathgrid *g, int ox, int oy) {
    // out of bounds: no obstacle
    if (ox < 0 || oy < 0 || ox >= g->width || oy >= g->height) {
        return 0;
    }

    uint32_t i = (uint32_t)ox * (uint32_t)g->height + (uint32_t)oy;

    return (g->bits[i >> 3] & (0x80u >> (i & 7))) != 0;
}

static int get_obstacle(const sp_pathgrid *g, int game_x, int game_y,
                        int offset_x, int offset_y) {
    int ox = g->width - (game_x + 1) * 2;
    int oy = game_y * 2;

    return grid_get(g, ox + offset_x, oy + offset_y);
}

int sp_path_valid_step(const sp_pathgrid *g, int start_x, int start_y,
                       int delta_x, int delta_y) {
    int end_x = start_x + delta_x;
    int end_y = start_y + delta_y;
    int ox = g->width - (end_x + 1) * 2;
    int oy = end_y * 2;

    // whole destination tile is blocked
    if (grid_get(g, ox, oy) && grid_get(g, ox + 1, oy) &&
        grid_get(g, ox, oy + 1) && grid_get(g, ox + 1, oy + 1)) {
        return 0;
    }

    if (delta_x == 0 && delta_y == -1) {
        // north: current tile, horizontal wall
        if (get_obstacle(g, start_x, start_y, 0, 0)) {
            return 0;
        }
    } else if (delta_x == 1 && delta_y == -1) {
        if (get_obstacle(g, start_x, start_y, 0, 0) ||
            get_obstacle(g, end_x, start_y, 1, 0) ||
            get_obstacle(g, end_x, end_y, 1, 1)) {
            return 0;
        }
    } else if (delta_x == 1 && delta_y == 0) {
        if (get_obstacle(g, end_x, start_y, 1, 1)) {
            return 0;
        }
    } else if (delta_x == 1 && delta_y == 1) {
        if (get_obstacle(g, start_x, end_y, 0, 0) ||
            get_obstacle(g, end_x, start_y, 1, 1) ||
            get_obstacle(g, end_x, end_y, 1, 0)) {
            return 0;
        }
    } else if (delta_x == 0 && delta_y == 1) {
        if (get_obstacle(g, end_x, end_y, 0, 0)) {
            return 0;
        }
    } else if (delta_x == -1 && delta_y == 1) {
        if (get_obstacle(g, start_x, start_y, 1, 1) ||
            get_obstacle(g, start_x, end_y, 1, 0)) {
            return 0;
        }
    } else if (delta_x == -1 && delta_y == 0) {
        if (get_obstacle(g, start_x, start_y, 1, 1)) {
            return 0;
        }
    } else if (delta_x == -1 && delta_y == -1) {
        if (get_obstacle(g, start_x, start_y, 0, 0) ||
            get_obstacle(g, start_x, end_y, 1, 1) ||
            get_obstacle(g, end_x, start_y, 0, 0)) {
            return 0;
        }
    }

    return 1;
}

// A* pathfinding

// step directions, in order
static const int8_t DIRECTIONS[8][2] = {{0, -1}, {0, 1},  {-1, 0}, {1, 0},
                                        {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

typedef struct pf_node {
    uint32_t key; // (x << 16) | y, +1 so 0 = empty slot
    int32_t g;
    int16_t px, py;
    int8_t dx, dy;
} pf_node;

typedef struct pf_heap_item {
    int32_t f;
    int32_t seq;
    int16_t x, y;
} pf_heap_item;

// scratch buffers, kept and grown to the largest request
static pf_node *table = NULL;
static uint32_t table_cap = 0;
static pf_heap_item *heap = NULL;
static uint32_t heap_cap = 0;

static uint32_t hash_key(uint32_t key) {
    key ^= key >> 16;
    key *= 0x7feb352du;
    key ^= key >> 15;
    key *= 0x846ca68bu;
    key ^= key >> 16;
    return key;
}

static pf_node *table_find(uint32_t key) {
    uint32_t mask = table_cap - 1;
    uint32_t i = hash_key(key) & mask;

    for (;;) {
        pf_node *n = &table[i];

        if (n->key == 0) {
            return NULL;
        }
        if (n->key == key) {
            return n;
        }
        i = (i + 1) & mask;
    }
}

static pf_node *table_insert(uint32_t key) {
    uint32_t mask = table_cap - 1;
    uint32_t i = hash_key(key) & mask;

    for (;;) {
        pf_node *n = &table[i];

        if (n->key == 0) {
            n->key = key;
            return n;
        }
        i = (i + 1) & mask;
    }
}

// order: smaller f first, then smaller seq
static int heap_less(const pf_heap_item *a, const pf_heap_item *b) {
    return a->f < b->f || (a->f == b->f && a->seq < b->seq);
}

static void heap_push(uint32_t *size, pf_heap_item item) {
    uint32_t i = (*size)++;
    heap[i] = item;

    while (i > 0) {
        uint32_t p = (i - 1) >> 1;

        // stop when parent <= child (f, then seq)
        if (heap[p].f < heap[i].f ||
            (heap[p].f == heap[i].f && heap[p].seq <= heap[i].seq)) {
            break;
        }

        pf_heap_item t = heap[p];
        heap[p] = heap[i];
        heap[i] = t;
        i = p;
    }
}

static pf_heap_item heap_pop(uint32_t *size) {
    pf_heap_item top = heap[0];
    pf_heap_item last = heap[--(*size)];

    if (*size > 0) {
        heap[0] = last;
        uint32_t i = 0;
        uint32_t n = *size;

        for (;;) {
            uint32_t l = 2 * i + 1;
            uint32_t r = l + 1;
            uint32_t s = i;

            if (l < n && heap_less(&heap[l], &heap[s])) {
                s = l;
            }
            if (r < n && heap_less(&heap[r], &heap[s])) {
                s = r;
            }
            if (s == i) {
                break;
            }

            pf_heap_item t = heap[s];
            heap[s] = heap[i];
            heap[i] = t;
            i = s;
        }
    }

    return top;
}

static int is_goal(int mode, int x, int y, int gx, int gy) {
    if (mode == SP_PATH_GOAL_ADJACENT) {
        int ax = x > gx ? x - gx : gx - x;
        int ay = y > gy ? y - gy : gy - y;

        return ax + ay == 1;
    }

    return x == gx && y == gy;
}

static int is_blocked(const int32_t *blocked, int blocked_count, int x,
                      int y) {
    for (int i = 0; i < blocked_count; i++) {
        if (blocked[i * 2] == x && blocked[i * 2 + 1] == y) {
            return 1;
        }
    }

    return 0;
}

static uint32_t rng_next(uint32_t *state) {
    // xorshift32
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

int sp_pathfind(const sp_pathgrid *grid, int sx, int sy, int gx, int gy,
                int goal_mode, int max_nodes, const int32_t *blocked,
                int blocked_count, uint32_t jitter_seed, int8_t *out_steps,
                int out_cap) {
    if (grid == NULL || grid->bits == NULL || max_nodes < 0) {
        return -1;
    }

    // each expansion adds at most 8 records, +1 for the start
    uint32_t need_nodes = (uint32_t)(max_nodes + 1) * 8u + 2u;
    uint32_t cap = 1024;

    while (cap < need_nodes * 2u) {
        cap <<= 1;
    }

    if (cap > table_cap) {
        free(table);
        table = (pf_node *)malloc((size_t)cap * sizeof(pf_node));
        table_cap = table != NULL ? cap : 0;
    }
    if (need_nodes > heap_cap) {
        free(heap);
        heap = (pf_heap_item *)malloc((size_t)need_nodes * sizeof(pf_heap_item));
        heap_cap = heap != NULL ? need_nodes : 0;
    }
    if (table == NULL || heap == NULL) {
        return -1;
    }

    memset(table, 0, (size_t)table_cap * sizeof(pf_node));

    // neighbour order: DIRECTIONS, or shuffled when jittered
    int8_t dirs[8][2];
    memcpy(dirs, DIRECTIONS, sizeof(dirs));

    if (jitter_seed != 0) {
        uint32_t state = jitter_seed;

        for (int i = 7; i > 0; i--) {
            int j = (int)(rng_next(&state) % (uint32_t)(i + 1));
            int8_t tx = dirs[i][0], ty = dirs[i][1];
            dirs[i][0] = dirs[j][0];
            dirs[i][1] = dirs[j][1];
            dirs[j][0] = tx;
            dirs[j][1] = ty;
        }
    }

#define KEY(x, y) ((((uint32_t)(x) & 0xffffu) << 16) | ((uint32_t)(y) & 0xffffu)) + 1u
#define HEUR(x, y)                                                             \
    (((x) > gx ? (x) - gx : gx - (x)) > ((y) > gy ? (y) - gy : gy - (y))       \
         ? ((x) > gx ? (x) - gx : gx - (x))                                    \
         : ((y) > gy ? (y) - gy : gy - (y)))

    uint32_t start_key = KEY(sx, sy);
    pf_node *start = table_insert(start_key);
    start->g = 0;
    start->px = -1;
    start->py = -1;
    start->dx = 0;
    start->dy = 0;

    uint32_t heap_size = 0;
    int32_t seq = 0;
    pf_heap_item first = {HEUR(sx, sy), seq, (int16_t)sx, (int16_t)sy};
    heap_push(&heap_size, first);

    int nodes = 0;

    while (heap_size > 0) {
        pf_heap_item top = heap_pop(&heap_size);
        int x = top.x;
        int y = top.y;

        if (!(x == sx && y == sy) && is_goal(goal_mode, x, y, gx, gy)) {
            // reconstruct backwards, then reverse into out_steps
            int count = 0;
            uint32_t key = KEY(x, y);

            while (key != start_key) {
                pf_node *n = table_find(key);

                if (n == NULL) {
                    return -1;
                }
                if (count < out_cap) {
                    out_steps[count * 2] = n->dx;
                    out_steps[count * 2 + 1] = n->dy;
                }
                count++;
                key = KEY(n->px, n->py);
            }

            if (count > out_cap) {
                return -1; // caller's buffer too small, treat as no path
            }

            for (int i = 0, j = count - 1; i < j; i++, j--) {
                int8_t tx = out_steps[i * 2], ty = out_steps[i * 2 + 1];
                out_steps[i * 2] = out_steps[j * 2];
                out_steps[i * 2 + 1] = out_steps[j * 2 + 1];
                out_steps[j * 2] = tx;
                out_steps[j * 2 + 1] = ty;
            }

            return count;
        }

        if (nodes > max_nodes) {
            break;
        }
        nodes++;

        pf_node *cur = table_find(KEY(x, y));
        int g = cur != NULL ? cur->g : 0;

        for (int d = 0; d < 8; d++) {
            int dx = dirs[d][0];
            int dy = dirs[d][1];
            int nx = x + dx;
            int ny = y + dy;
            uint32_t nk = KEY(nx, ny);

            if (table_find(nk) != NULL) {
                continue;
            }
            if (!sp_path_valid_step(grid, x, y, dx, dy)) {
                continue;
            }
            if (blocked_count > 0 && is_blocked(blocked, blocked_count, nx, ny) &&
                !is_goal(goal_mode, nx, ny, gx, gy)) {
                continue;
            }

            pf_node *n = table_insert(nk);
            n->px = (int16_t)x;
            n->py = (int16_t)y;
            n->dx = (int8_t)dx;
            n->dy = (int8_t)dy;
            n->g = g + 1;
            seq++;

            pf_heap_item item = {g + 1 + HEUR(nx, ny), seq, (int16_t)nx,
                                 (int16_t)ny};
            heap_push(&heap_size, item);
        }
    }

#undef KEY
#undef HEUR

    return -1;
}
