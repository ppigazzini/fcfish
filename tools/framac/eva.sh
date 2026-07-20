#!/usr/bin/env bash
# Prove runtime safety of the board-layer pure helpers with Frama-C's Eva plugin.
#
# tools/framac/eva_harness.c drives every function under test with its full valid input
# interval, so "0 alarms" from Eva is a proof that none of them can commit a runtime
# error -- out-of-range shift, signed overflow, out-of-bounds access -- on any input in
# that domain. This gate fails if Eva raises even one alarm, or if frama-c aborts.
#
# It shares the parse gate's analyser setup (gcc_x86_64 machdep for the builtin bit ops
# and extended alignment; the scalar SIMD path). attacks.c is linked so that LineBB has
# a definition for the `aligned` target -- the array's bounds, not its filled contents,
# are what the safety proof needs, so attacks_init is never run. attacks_bb itself is
# not yet covered: proving it clean means discharging attacks_init's magic-table search,
# which needs ACSL assigns/allocation specs the tree does not carry yet.
set -euo pipefail

cd "$(dirname "$0")/../.."

# Frama-C lives in the opam switch; treat its absence as a SKIP (exit 127), as every
# other build.sh gate does for a missing tool.
if command -v opam > /dev/null 2>&1; then
  eval "$(opam env)"
fi
if ! command -v frama-c > /dev/null 2>&1; then
  echo "frama-c not found -- activate the opam switch (eval \$(opam env)) or install it." >&2
  exit 127
fi

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# -eva-precision 6 keeps every input interval exact through the safety targets;
# -eva-slevel 5000 lets Frama_C_interval_split enumerate the codec's 64x64 move states
# so the correctness round-trips (proved, not assumed) discharge. Eva exits 0 even with
# alarms or unproven assertions -- they are analysis results, not tool errors -- so the
# pass/fail signal is Eva's own summary, checked below, not the exit code.
# -no-warn-right-shift-negative: the NNUE kernels right-shift signed accumulator lanes,
# relying on the arithmetic shift both target compilers provide (implementation-defined
# under C17, see nnue/simd.h) -- so the shift-of-negative that the flag would flag is the
# defined behaviour here, not an alarm. The board helpers never shift a negative, so this
# only bears on the NNUE targets. tools/framac/fc_stubs.h is force-included for the
# __builtin_memcpy-to-memcpy map its comment explains (the NNUE SIMD load/store need it).
STUBS="$PWD/tools/framac/fc_stubs.h"
if ! frama-c \
  -std c17 \
  -machdep gcc_x86_64 \
  -no-warn-right-shift-negative \
  -cpp-extra-args="-Isrc -D_POSIX_C_SOURCE=200809L -DFCFISH_SIMD_SCALAR -include $STUBS" \
  tools/framac/eva_harness.c src/engine/board/attacks.c src/engine/eval/nnue/nnue_affine.c \
  -main eva_main -eva -eva-precision 6 -eva-slevel 5000 \
  > "$log" 2>&1; then
  cat "$log"
  echo "eva: frama-c aborted" >&2
  exit 1
fi

alarms=$(grep -oE '[0-9]+ alarms? generated' "$log" | grep -oE '^[0-9]+' | head -1 || true)
proven=$(grep -oE '[0-9]+% of the logical properties' "$log" | grep -oE '^[0-9]+' | head -1 || true)
# The "Assertions" line reads: "<valid> valid  <unknown> unknown  <invalid> invalid ...".
# A functional round-trip that Eva could not discharge lands in unknown/invalid and is
# NOT counted among the alarms, so the gate must inspect it directly.
assert_line=$(grep -E 'Assertions +[0-9]+ valid' "$log" | tail -1 || true)
unproven=$(echo "$assert_line" \
  | awk '{for (i=1;i<=NF;i++) if ($i=="unknown"||$i=="invalid") s+=$(i-1); print s+0}')
echo "eva: ${alarms:-?} alarm(s), ${unproven:-?} unproven assertion(s), ${proven:-?}% of reached properties proven"

if [[ "${alarms:-x}" != "0" ]]; then
  echo "--- Eva alarms ---" >&2
  grep -iE 'assert|alarm' "$log" | grep -viE '0 alarms|remove-redundant|precision-settings' >&2 || true
  echo "FAIL: Eva reported ${alarms:-an unknown number of} alarm(s)" >&2
  exit 1
fi
if [[ "${unproven:-x}" != "0" ]]; then
  echo "--- unproven assertions ---" >&2
  grep -iE 'got status (unknown|invalid)' "$log" >&2 || true
  echo "FAIL: Eva left ${unproven} assertion(s) unproven" >&2
  exit 1
fi
