// Frama-C Eva harness for memory.c's aligned-allocation path.
//
// aligned_large_pages_alloc rounds the request up to a whole number of 2 MiB pages and
// hands it to posix_memalign (through std_aligned_alloc); large_page_size and
// has_large_pages return constants; aligned_large_pages_free frees. This harness proves
// that path commits no runtime error for any request in a wide interval -- the size
// rounding trips no signed overflow or bad shift, and the alignment handed to
// posix_memalign is a valid one (a power of two and a multiple of sizeof(void*)), which is
// posix_memalign's own precondition. Eva models posix_memalign, so the allocation itself
// is tracked, unlike the mmap path below.
//
// NOT COVERED: page_alloc_default / page_free_default, the mmap-backed default page
// allocator. Frama-C's kernel does not model mmap's returned pointer as valid memory, so
// the header write `*(size_t*)raw = total` cannot be proved in bounds -- verified: it is
// the ONLY alarm memory.c raises, two writes at memory.c:87. That is the same
// syscall-return-validity wall the numa realloc insertion hits (docs 10), and it is left to
// review and the unit tests. So memory.c is split: the posix_memalign allocator is proved
// here, the mmap allocator is not provable by Eva.
#include "platform/memory.c"

#include "__fc_builtin.h"

int eva_main(void) {
    // The whole aligned-allocation path over an arbitrary request, including 0.
    void *p = aligned_large_pages_alloc(Frama_C_interval(0, 1000000000));
    aligned_large_pages_free(p);

    // The primitive underneath it, at the cache-line alignment its other caller uses.
    void *a = std_aligned_alloc(64, Frama_C_interval(0, 1000000));
    std_aligned_free(a);

    (void) large_page_size();
    (void) has_large_pages();
    return 0;
}
