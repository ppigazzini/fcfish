#!/usr/bin/env bash
# Parse and typecheck the whole engine under Frama-C's kernel.
#
# This is the foundation gate for Frama-C work: it asserts the tree is a well-formed
# C17 program to the ANALYSER, not merely to clang -- a precondition for any later
# ACSL contract, EVA value analysis or WP proof. It runs no analysis plugin; a clean
# exit means every translation unit parsed, typechecked, and linked into one project.
#
# Two facts about the analyser shape this run:
#
#   * SCALAR SIMD. Frama-C's kernel cannot model GCC vector extensions or the x86
#     intrinsic headers, so the sources are built through the -DFCFISH_SIMD_SCALAR
#     path (the same one `./build.sh simd-scalar` proves is bit-identical to the
#     vector path). Only src/engine/eval/nnue/simd.h differs between the two.
#
#   * gcc_x86_64 MACHDEP. It matches the clang/gcc target the engine ships on, so the
#     __builtin_* bit ops resolve and the 64-byte `alignas(CACHE_LINE_SIZE)` is
#     accepted -- the default x86_64 machdep caps alignment at 16 and knows no
#     builtins.
#
# tools/framac/fc_stubs.h is force-included to supply the Linux CPU-affinity surface
# (cpu_set_t and the CPU_* macros) that Frama-C's bundled libc omits; it is seen only
# here, never by clang or gcc.
#
# One 128-bit multiply (src/engine/search/tt.c mul_hi64) has no __uint128_t under the
# kernel; it carries a __FRAMAC__-guarded 64-bit equivalent for this run alone.
set -euo pipefail

cd "$(dirname "$0")/../.."

# Frama-C lives in the opam switch; make it reachable, then treat its absence the way
# every other build.sh gate treats a missing tool -- exit 127 so `parity` records a
# SKIP instead of a pass.
if command -v opam > /dev/null 2>&1; then
  eval "$(opam env)"
fi
if ! command -v frama-c > /dev/null 2>&1; then
  echo "frama-c not found -- activate the opam switch (eval \$(opam env)) or install it." >&2
  exit 127
fi

# Take the source set straight from build.sh's SOURCES array, so the gate can never
# drift from what actually compiles.
mapfile -t SOURCES < <(
  awk '/^SOURCES=\(/{f=1} f{print} /^\)/{if(f)exit}' build.sh \
    | grep -oE 'src/[a-zA-Z0-9_/]+\.c'
)
if [[ ${#SOURCES[@]} -eq 0 ]]; then
  echo "could not read SOURCES from build.sh" >&2
  exit 1
fi

STUBS="$PWD/tools/framac/fc_stubs.h"

# -kernel-warn-key parser:decimal-float=inactive silences the one benign category the
# parse raises: a float literal like 0.01 is not exactly representable, which is true
# of every such constant and says nothing about this code. Every other diagnostic is
# left visible. A non-zero exit is a real parse/type/link failure.
frama-c \
  -std c17 \
  -machdep gcc_x86_64 \
  -kernel-warn-key parser:decimal-float=inactive \
  -cpp-extra-args="-Isrc -D_POSIX_C_SOURCE=200809L -DFCFISH_SIMD_SCALAR -include $STUBS" \
  "${SOURCES[@]}"
