// Frama-C parse shims. Declare the GNU/Linux CPU-affinity surface that Frama-C's
// bundled libc omits, so src/platform/thread.c and numa.c reach the analyser. This
// header is force-included under the analyser only (see tools/framac/parse.sh) and
// is never seen by clang or gcc, so it cannot affect the shipped engine.
#ifndef FCFISH_FRAMAC_STUBS_H
#define FCFISH_FRAMAC_STUBS_H

#include <stddef.h>

#ifndef CPU_SETSIZE
    #define CPU_SETSIZE 1024
typedef struct {
    unsigned long __bits[CPU_SETSIZE / (8 * sizeof(unsigned long))];
} cpu_set_t;
    #define CPU_ZERO(set)     ((void) (set))
    #define CPU_SET(cpu, set) ((void) (cpu), (void) (set))
    #define CPU_ISSET(cpu, set) ((cpu) == (cpu) && (set) != (set))
    #define CPU_COUNT(set)    (0)
#endif

extern int sched_getaffinity(int pid, size_t cpusetsize, cpu_set_t *mask);
extern int sched_setaffinity(int pid, size_t cpusetsize, const cpu_set_t *mask);

// The NNUE SIMD load/store spell their copies as __builtin_memcpy, which Frama-C's
// kernel has no model for -- so it cannot see the copy propagate initialization, and Eva
// reports the destination as uninitialised. The libc memcpy Frama-C DOES model carries a
// spec that propagates it, and the two are the same operation, so under the analyser map
// one onto the other. Seen only here, never by clang or gcc.
#include <string.h>
#define __builtin_memcpy(d, s, n) memcpy((d), (s), (n))

// numa_config_distribute_threads seeds its best-fill accumulator with __builtin_inff(),
// which Frama-C's kernel has no model for either -- without a spec Eva treats the seed as
// an arbitrary float (possibly NaN) and the first `fill < best_fill` trips \is_finite. The
// builtin returns +infinity, so give it that contract; the caller's comparison against a
// finite fill is then well defined (and eva.sh runs that harness with -warn-special-float
// nan, which permits the deliberate infinity while still catching a NaN). Analyser only.
/*@ assigns \nothing; ensures \is_plus_infinity(\result); */
extern float __builtin_inff(void);

#endif  // FCFISH_FRAMAC_STUBS_H
