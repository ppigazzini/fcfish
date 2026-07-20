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

#endif  // FCFISH_FRAMAC_STUBS_H
