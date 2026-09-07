// size-class pool for the embedded QuickJS runtime (see sp-alloc.h).
// 8-byte header before each user pointer:
//   uint32_t cls  = size class 1..SP_ALLOC_NCLASSES, or SP_ALLOC_LARGE
//   uint32_t size = requested size for large blocks, 0 for small
// small caps: 16-byte steps to 512, then 128-byte steps to SP_ALLOC_SMALL_MAX;
// each class keeps a free list, misses carve from the current slab
#include "sp-alloc.h"

#include <stdlib.h>
#include <string.h>

#define SP_ALLOC_SLAB (256 * 1024)
#define SP_ALLOC_LARGE 0xFFFFFFFFu
#define SP_ALLOC_HDR 8

// classes 1..32: 16..512 by 16; classes 33..60: 640..4096 by 128
#define SP_ALLOC_NCLASSES 60

typedef struct sp_hdr {
    uint32_t cls;
    uint32_t size;
} sp_hdr;

typedef struct sp_free {
    struct sp_free *next;
} sp_free;

static sp_free *sp_free_head[SP_ALLOC_NCLASSES + 1];
static uint8_t *sp_slab_cur = NULL;
static size_t sp_slab_left = 0;

static inline uint32_t sp_class_of(size_t size) {
    if (size <= 512) {
        uint32_t c = (uint32_t)((size + 15) >> 4);
        return c == 0 ? 1 : c;
    }
    return 32 + (uint32_t)((size - 512 + 127) >> 7);
}

static inline size_t sp_class_cap(uint32_t cls) {
    return cls <= 32 ? (size_t)cls * 16 : 512 + (size_t)(cls - 32) * 128;
}

static void *sp_carve(size_t bytes) {
    if (sp_slab_left < bytes) {
        size_t want = bytes > SP_ALLOC_SLAB ? bytes : SP_ALLOC_SLAB;
        uint8_t *slab = (uint8_t *)malloc(want);
        if (slab == NULL) {
            return NULL;
        }
        sp_slab_cur = slab;
        sp_slab_left = want;
    }
    void *p = sp_slab_cur;
    sp_slab_cur += bytes;
    sp_slab_left -= bytes;
    return p;
}

void *sp_alloc_malloc(size_t size) {
    if (size <= SP_ALLOC_SMALL_MAX) {
        uint32_t cls = sp_class_of(size);
        sp_hdr *h;
        sp_free *f = sp_free_head[cls];
        if (f != NULL) {
            sp_free_head[cls] = f->next;
            h = (sp_hdr *)((uint8_t *)f - SP_ALLOC_HDR);
        } else {
            h = (sp_hdr *)sp_carve(SP_ALLOC_HDR + sp_class_cap(cls));
            if (h == NULL) {
                return NULL;
            }
        }
        h->cls = cls;
        h->size = 0;
        return (uint8_t *)h + SP_ALLOC_HDR;
    }
    sp_hdr *h = (sp_hdr *)malloc(SP_ALLOC_HDR + size);
    if (h == NULL) {
        return NULL;
    }
    h->cls = SP_ALLOC_LARGE;
    h->size = (uint32_t)size;
    return (uint8_t *)h + SP_ALLOC_HDR;
}

void *sp_alloc_calloc(size_t count, size_t size) {
    size_t total;
    if (count != 0 && size > ((size_t)-1) / count) {
        return NULL; // overflow
    }
    total = count * size;
    void *p = sp_alloc_malloc(total);
    if (p != NULL) {
        memset(p, 0, total);
    }
    return p;
}

void sp_alloc_free(void *ptr) {
    if (ptr == NULL) {
        return;
    }
    sp_hdr *h = (sp_hdr *)((uint8_t *)ptr - SP_ALLOC_HDR);
    if (h->cls == SP_ALLOC_LARGE) {
        free(h);
        return;
    }
    sp_free *f = (sp_free *)ptr;
    f->next = sp_free_head[h->cls];
    sp_free_head[h->cls] = f;
}

size_t sp_alloc_usable_size(const void *ptr) {
    if (ptr == NULL) {
        return 0;
    }
    const sp_hdr *h = (const sp_hdr *)((const uint8_t *)ptr - SP_ALLOC_HDR);
    return h->cls == SP_ALLOC_LARGE ? (size_t)h->size : sp_class_cap(h->cls);
}

void *sp_alloc_realloc(void *ptr, size_t size) {
    if (ptr == NULL) {
        return sp_alloc_malloc(size);
    }
    if (size == 0) {
        sp_alloc_free(ptr);
        return NULL;
    }
    sp_hdr *h = (sp_hdr *)((uint8_t *)ptr - SP_ALLOC_HDR);
    if (h->cls == SP_ALLOC_LARGE) {
        if (size > SP_ALLOC_SMALL_MAX) {
            sp_hdr *nh = (sp_hdr *)realloc(h, SP_ALLOC_HDR + size);
            if (nh == NULL) {
                return NULL;
            }
            nh->size = (uint32_t)size;
            return (uint8_t *)nh + SP_ALLOC_HDR;
        }
        // shrinking into the pool
        void *np = sp_alloc_malloc(size);
        if (np == NULL) {
            return NULL;
        }
        memcpy(np, ptr, size);
        free(h);
        return np;
    }
    size_t cap = sp_class_cap(h->cls);
    if (size <= cap) {
        return ptr; // still fits its class
    }
    void *np = sp_alloc_malloc(size);
    if (np == NULL) {
        return NULL;
    }
    memcpy(np, ptr, cap);
    sp_alloc_free(ptr);
    return np;
}

// QuickJS adapters
void *sp_alloc_js_calloc(void *opaque, size_t count, size_t size) {
    (void)opaque;
    return sp_alloc_calloc(count, size);
}
void *sp_alloc_js_malloc(void *opaque, size_t size) {
    (void)opaque;
    return sp_alloc_malloc(size);
}
void sp_alloc_js_free(void *opaque, void *ptr) {
    (void)opaque;
    sp_alloc_free(ptr);
}
void *sp_alloc_js_realloc(void *opaque, void *ptr, size_t size) {
    (void)opaque;
    return sp_alloc_realloc(ptr, size);
}
size_t sp_alloc_js_usable_size(const void *ptr) {
    return sp_alloc_usable_size(ptr);
}

