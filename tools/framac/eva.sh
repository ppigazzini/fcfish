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

# -eva-precision 6 is enough to keep every input interval exact through these helpers;
# raise it only if a future target loses precision (widening) and reports a spurious
# alarm. Eva exits 0 even with alarms -- alarms are analysis results, not tool errors --
# so the pass/fail signal is the alarm count in its own summary, not the exit code.
if ! frama-c \
  -std c17 \
  -machdep gcc_x86_64 \
  -cpp-extra-args="-Isrc -D_POSIX_C_SOURCE=200809L -DFCFISH_SIMD_SCALAR" \
  tools/framac/eva_harness.c src/engine/board/attacks.c \
  -main eva_main -eva -eva-precision 6 \
  > "$log" 2>&1; then
  cat "$log"
  echo "eva: frama-c aborted" >&2
  exit 1
fi

alarms=$(grep -oE '[0-9]+ alarms? generated' "$log" | grep -oE '^[0-9]+' | head -1 || true)
proven=$(grep -oE '[0-9]+% of the logical properties' "$log" | grep -oE '^[0-9]+' | head -1 || true)
echo "eva: ${alarms:-?} alarm(s), ${proven:-?}% of reached properties proven"

if [[ "${alarms:-x}" != "0" ]]; then
  echo "--- Eva alarms ---" >&2
  grep -iE 'assert|alarm' "$log" | grep -viE '0 alarms|remove-redundant|precision-settings' >&2 || true
  echo "FAIL: Eva reported ${alarms:-an unknown number of} alarm(s)" >&2
  exit 1
fi
