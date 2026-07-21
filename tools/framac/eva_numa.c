// Frama-C Eva harness for the platform NumaConfig pure logic: the NumaPolicy string
// parser's input safety, and the two thread-placement decisions
// numa_config_suggests_binding_threads and numa_config_distribute_threads.
//
// numa_config_from_string parses an untrusted "NumaPolicy" string -- nodes split on ':',
// each a comma list of decimal indices or "lo-hi" ranges (e.g. "0-3,8:4-7"). The tokenizer
// (for_each_index), the per-element whitespace trim, and the decimal/range parse (parse_uint
// and parse_element) all index into the caller's bytes; a malformed string must never drive
// those reads off the buffer or overflow the decimal accumulator. This harness feeds
// adversarial and boundary strings through that parse layer and proves -- through 0 alarms --
// that no read leaves the buffer and no accumulator overflows.
//
// It covers the parse layer with a NON-ALLOCATING sink, deliberately. The full
// numa_config_from_string also inserts each parsed CPU into growable arrays through
// numa_config_add_cpu_to_node, whose realloc-backed growth Eva's value analysis cannot
// track (its allocation model loses the capacity >= count invariant across the realloc, so
// it reports false out-of-bounds writes); that growth invariant is left to review and the
// unit tests. The counting sink walks every parsed index without allocating, so the string
// handling -- the part exposed to untrusted input -- is proved on its own.
//
// This #includes numa.c to reach the file-static for_each_index / parse_element; the sink
// never refuses, and numa_config_add_cpu_to_node with every realloc and syscall path is
// unreached from eva_main, so Eva never analyses them. Run it with tools/framac/eva.sh.
//
// Bounded-model-checking coverage, as with eva_fen.c: the parse's read safety on these
// representative inputs, not a proof over every possible string (a symbolic policy string
// explodes Eva's state space). Ranges in the walked strings are kept small because the
// counting sink never stops the walk.
#include "platform/numa.c"

#include "__fc_builtin.h"

// Count each parsed index and keep walking; refusing would only cut the walk short and hide
// reads, which is the opposite of what this proves.
static bool count_sink(void *ctx, size_t index) {
    (void) index;
    ++*(size_t *) ctx;
    return true;
}

// Split S on ':' and run each node segment through for_each_index, exactly as
// numa_config_from_string does, but with the counting sink in place of the CPU insert.
static void walk(const char *s, size_t len) {
    size_t start = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i != len && s[i] != ':')
            continue;
        size_t n = 0;
        (void) for_each_index(s + start, i - start, count_sink, &n, NULL);
        start = i + 1;
    }
}

// numa_config_suggests_binding_threads reads each node's `count` and decides whether to
// bind -- a fill-ratio comparison in `double` plus integer thresholds. Prove it commits no
// runtime error (no out-of-bounds node read, no overflow, no division that is not the
// deliberate one) over a hand-built config: NODE_COUNT concrete (the loop bound), each
// node's count an interval, custom_affinity and num_threads free. Node 0 is kept non-empty
// so `largest_node_size >= 1` and the `count / largest` ratio never divides by zero -- which
// matches how the function is called, always after numa_config_remove_empty_nodes has
// dropped empty nodes. The nodes' cpus pointers are never read here, so they stay null.
static void check_suggests_binding(size_t node_count) {
    NumaNode nodes[8];
    for (size_t n = 0; n < 8; ++n) {
        nodes[n].cpus = NULL;
        nodes[n].capacity = 0;
        nodes[n].count = 0;
    }
    nodes[0].count = Frama_C_interval(1, 4096);  // a non-empty node: largest_node_size >= 1
    for (size_t n = 1; n < node_count; ++n)
        nodes[n].count = Frama_C_interval(0, 4096);  // may be empty: the small-node path

    NumaConfig cfg;
    numa_config_init(&cfg);
    cfg.nodes = nodes;
    cfg.node_count = node_count;
    cfg.custom_affinity = (bool) Frama_C_interval(0, 1);
    (void) numa_config_suggests_binding_threads(&cfg, Frama_C_interval(0, 100000));
}

// numa_config_distribute_threads assigns each of NUM_THREADS threads to a node by lowest
// fill ratio, writing OUT_NODES[0 .. NUM_THREADS-1] and reading each node's `count`. Prove
// it commits no runtime error over a hand-built config: no out-of-bounds write to out_nodes
// (num_threads bounds the loop and the buffer), no out-of-bounds read of the calloc'd
// occupation array or of nodes[], and no bad float divide. calloc IS modelled by Eva here
// (a fresh fixed-size block, unlike the parser's growth realloc); node 0 is kept non-empty
// so the `(occupation+1) / count` ratio never divides by zero, matching the post-
// remove_empty_nodes domain. The +inf seed is specified in fc_stubs.h.
static void check_distribute(size_t node_count, size_t num_threads) {
    NumaNode nodes[8];
    for (size_t n = 0; n < 8; ++n) {
        nodes[n].cpus = NULL;
        nodes[n].capacity = 0;
        nodes[n].count = 0;
    }
    for (size_t n = 0; n < node_count; ++n)
        nodes[n].count = Frama_C_interval(1, 4096);

    NumaConfig cfg;
    numa_config_init(&cfg);
    cfg.nodes = nodes;
    cfg.node_count = node_count;
    size_t out[16];
    (void) numa_config_distribute_threads(&cfg, num_threads, out);
}

int eva_main(void) {
    // Valid, boundary and adversarial NumaPolicy strings. The length passed excludes the
    // literal's trailing NUL, matching numa_config_from_string's caller.
    walk("0-3,8:4-7", 9);              // two nodes: a range plus a list, and a second range
    walk("", 0);                       // empty string
    walk(":::", 3);                    // only node separators, every segment empty
    walk(",,,", 3);                    // only element separators, every element empty
    walk("0,0", 3);                    // a repeated index (the sink ignores the conflict)
    walk("7-3", 3);                    // reversed range: hi-lo wraps and fails the cap
    walk("1-2-3", 5);                  // three dash-parts: contributes nothing
    walk("  0 , 1 : 2 ", 12);          // whitespace around elements, and a /sys-style space
    walk("99999999999999999999", 20);  // decimal overflow: parse_uint's guard must refuse
    walk("-", 1);                      // a lone dash: empty head and empty tail
    walk("0-", 2);                     // empty tail
    walk("-5", 2);                     // empty head
    walk("a:b-c", 5);                  // non-digit bytes throughout
    walk("18446744073709551615", 20);  // SIZE_MAX exactly: the largest value that parses

    // parse_element directly at the range-size cap boundary (NumaMaxRangeIndices), which the
    // walked strings avoid so the counting sink is not driven across a million indices. A
    // direct call parses without looping, so both sides of the cap are cheap to cover.
    size_t lo = 0;
    size_t hi = 0;
    (void) parse_element("0-1048575", 9, &lo, &hi);  // hi-lo == 2^20 - 1: passes the cap
    (void) parse_element("0-1048576", 9, &lo, &hi);  // hi-lo == 2^20: fails the cap
    (void) parse_element("18446744073709551616", 20, &lo, &hi);  // 2^64: overflows, refused

    // The binding decision over 1, 2, 4 and 8 nodes -- the full node-count loop range.
    check_suggests_binding(1);
    check_suggests_binding(2);
    check_suggests_binding(4);
    check_suggests_binding(8);

    // Thread distribution: the <=1-node memset path and the multi-node calloc path.
    check_distribute(1, 16);
    check_distribute(2, 16);
    check_distribute(4, 16);
    check_distribute(8, 16);
    return 0;
}
