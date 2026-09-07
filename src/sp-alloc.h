// pooled allocator for the embedded single-player QuickJS runtime.
// size-class pool: blocks up to SP_ALLOC_SMALL_MAX come from per-class free lists
// carved out of large slabs (O(1) alloc/free); bigger blocks use the system
// allocator. single-threaded: only the server thread uses it.
#ifndef _H_SP_ALLOC
#define _H_SP_ALLOC

#include <stddef.h>
#include <stdint.h>

#define SP_ALLOC_SMALL_MAX 4096

void *sp_alloc_malloc(size_t size);
void *sp_alloc_calloc(size_t count, size_t size);
void sp_alloc_free(void *ptr);
void *sp_alloc_realloc(void *ptr, size_t size);
size_t sp_alloc_usable_size(const void *ptr);

// QuickJS JSMallocFunctions-shaped adapters (opaque ignored)
void *sp_alloc_js_calloc(void *opaque, size_t count, size_t size);
void *sp_alloc_js_malloc(void *opaque, size_t size);
void sp_alloc_js_free(void *opaque, void *ptr);
void *sp_alloc_js_realloc(void *opaque, void *ptr, size_t size);
size_t sp_alloc_js_usable_size(const void *ptr);


#endif
