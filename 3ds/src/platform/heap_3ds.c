/*
 * Replacement allocator for the 3DS port.
 *
 * Routes every allocation entry point (malloc & friends plus newlib's
 * reentrant _*_r forms) into a Doug Lea mspace built over ONE big pool
 * taken from the libctru fake heap on first use. This gives the engine a
 * single deterministic ~90 MiB arena with no morecore edge cases.
 */

#define ONLY_MSPACES 1
#define MSPACES 1
#define HAVE_MMAP 0
#define HAVE_MORECORE 0
#define LACKS_SYS_MMAN_H
#define USE_LOCKS 1
#define NO_MALLOC_STATS 1
#define malloc_getpagesize 4096

#include "../../external/dlmalloc2.c"

#include <unistd.h>
#include <stdint.h>

extern char* fake_heap_start;
extern char* fake_heap_end;

static mspace g_mspace = 0;
static char* g_pool_base = 0;
static long g_pool_size = 0;

/* keep a small reserve for anything that still uses raw sbrk */
#define HEAP_RESERVE (64 * 1024)

static mspace get_mspace(void) {
    if (!g_mspace) {
        char* base = (char*)sbrk(0);
        ptrdiff_t avail = (fake_heap_end - base) - HEAP_RESERVE;
        if (avail < (1 << 20)) {
            static char emergency[256 * 1024];
            g_mspace = create_mspace_with_base(emergency, sizeof(emergency), 1);
        } else {
            while (avail >= (8 << 20) && sbrk(avail) == (void*)-1) {
                avail -= 2 << 20;
            }
            g_mspace = create_mspace_with_base(base, avail, 1);
            g_pool_base = base;
            g_pool_size = (long)avail;
        }
    }
    return g_mspace;
}

void vc3ds_heap_stats(size_t* total, size_t* used) {
    if (total) *total = g_mspace ? mspace_footprint(g_mspace) : 0;
    if (used && g_mspace) {
        mstate m = (mstate)g_mspace;
        *used = m->footprint - m->topsize;
    } else if (used) {
        *used = 0;
    }
}

/* ---- plain entry points ---- */
void* __wrap_malloc(size_t size) {
    return mspace_malloc(get_mspace(), size);
}
void __wrap_free(void* ptr) {
    if (ptr) mspace_free(get_mspace(), ptr);
}
void* __wrap_calloc(size_t nmemb, size_t size) {
    return mspace_calloc(get_mspace(), nmemb, size);
}
void* __wrap_realloc(void* ptr, size_t size) {
    return mspace_realloc(get_mspace(), ptr, size);
}
void* __wrap_memalign(size_t alignment, size_t size) {
    return mspace_memalign(get_mspace(), alignment, size);
}
void* __wrap_aligned_alloc(size_t alignment, size_t size) {
    return mspace_memalign(get_mspace(), alignment, size);
}
int __wrap_posix_memalign(void** memptr, size_t alignment, size_t size) {
    void* p = mspace_memalign(get_mspace(), alignment, size);
    if (!p) return 12; /* ENOMEM */
    *memptr = p;
    return 0;
}
size_t __wrap_malloc_usable_size(void* ptr) {
    return mspace_usable_size(ptr);
}

/* ---- newlib reentrant entry points ---- */
struct _reent;
void* __wrap__malloc_r(struct _reent* r, size_t size) {
    (void)r;
    return mspace_malloc(get_mspace(), size);
}
void __wrap__free_r(struct _reent* r, void* ptr) {
    (void)r;
    if (ptr) mspace_free(get_mspace(), ptr);
}
void* __wrap__calloc_r(struct _reent* r, size_t nmemb, size_t size) {
    (void)r;
    return mspace_calloc(get_mspace(), nmemb, size);
}
void* __wrap__realloc_r(struct _reent* r, void* ptr, size_t size) {
    (void)r;
    return mspace_realloc(get_mspace(), ptr, size);
}
void* __wrap__memalign_r(struct _reent* r, size_t alignment, size_t size) {
    (void)r;
    return mspace_memalign(get_mspace(), alignment, size);
}
