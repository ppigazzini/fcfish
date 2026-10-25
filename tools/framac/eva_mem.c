// Frama-C Eva harness for memory.c's allocation surface.
//
// std_aligned_alloc hands its request to posix_memalign; large_page_size and
// has_large_pages return constants; aligned_large_pages_alloc rounds a request up to whole
// 2 MiB pages and maps it, returning NULL for an empty one. This harness proves that the
// posix_memalign call meets the libc spec's preconditions and commits no runtime error for
// any request in a wide interval, and that the large-page surface's empty request maps
// nothing and frees cleanly. Eva does not model the block posix_memalign returns (it
// reports the spec unsupported and ignores its allocates clause), so what is proved is the
// call, not the allocation; and the spec answers a bad alignment with EINVAL rather than a
// failed precondition, so no alignment is proved valid here either.
//
// NOT COVERED: every non-empty large block. aligned_large_pages_alloc and the page_alloc
// seam both map it through map_large_aligned, and Frama-C's mmap spec says only that the
// call allocates its result, which Eva ignores -- so the header write
// `((size_t *) payload)[-1] = total` lands on no valid address and is a sure alarm, and
// Eva then drops the path as dead (verified at memory.c's header write, the only alarm the
// file raises). That is the same syscall-return-validity wall the numa realloc insertion
// hits (docs 11), and it is left to review, the unit tests and test_page_allocator's
// mapping count.
#include "platform/memory.c"

#include "__fc_builtin.h"

int eva_main(void) {
    // The cache-line allocator over an arbitrary request, including 0.
    void *a = std_aligned_alloc(64, Frama_C_interval(0, 1000000));
    std_aligned_free(a);

    // The empty large-page request: refused before any mapping, and the NULL it returns
    // is a valid argument to the free.
    aligned_large_pages_free(aligned_large_pages_alloc(0));

    (void) large_page_size();
    (void) has_large_pages();
    return 0;
}
