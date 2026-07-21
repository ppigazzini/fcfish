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
# are what the safety proof needs, so attacks_init is never run. attacks_bb's leaper arm
# is covered by the board harness and its slider arm by the `slider` harness below --
# both without attacks_init, because the magic-index bound is a property of the shift and
# block size, not of the magic search (see eva_slider.c).
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
COMMON=(
  -std c17
  -machdep gcc_x86_64
  -no-warn-right-shift-negative
  "-cpp-extra-args=-Isrc -D_POSIX_C_SOURCE=200809L -DFCFISH_SIMD_SCALAR -include $STUBS"
  -main eva_main -eva
)

# The codec proofs need -eva-precision 6 -eva-slevel 5000 to keep the case-split intervals
# exact; a harness over only concrete inputs (eva_numa) is sound at a far cheaper setting
# and slow at the expensive one, so precision/slevel are per-harness with these defaults.
# Eva is monotone -- 0 alarms at a coarser setting implies 0 at a finer one -- so a cheaper
# proof is a stronger one, never a weaker.
: "${EVA_PRECISION:=6}"
: "${EVA_SLEVEL:=5000}"

# Two harnesses, two analyses: the board/NNUE proofs and the movegen buffer-discipline
# proofs live in separate translation units (eva_movegen.c #includes movegen.c), so they
# cannot share one run. Each must be alarm-free with every assertion discharged.
run_harness() { # $1 = label; $2.. = frama-c source arguments
  local label=$1
  shift
  frama-c "${COMMON[@]}" -eva-precision "$EVA_PRECISION" -eva-slevel "$EVA_SLEVEL" \
    "$@" > "$log" 2>&1 || {
    cat "$log"
    echo "eva[$label]: frama-c aborted" >&2
    exit 1
  }

  local alarms assert_line unproven
  alarms=$(grep -oE '[0-9]+ alarms? generated' "$log" | grep -oE '^[0-9]+' | head -1 || true)
  # The "Assertions" line reads "<valid> valid  <unknown> unknown  <invalid> invalid ...".
  # An assertion Eva could not discharge lands in unknown/invalid and is NOT counted among
  # the alarms, so the gate inspects it directly.
  assert_line=$(grep -E 'Assertions +[0-9]+ valid' "$log" | tail -1 || true)
  unproven=$(echo "$assert_line" \
    | awk '{for (i=1;i<=NF;i++) if ($i=="unknown"||$i=="invalid") s+=$(i-1); print s+0}')
  echo "eva[$label]: ${alarms:-?} alarm(s), ${unproven:-?} unproven assertion(s)"

  if [[ "${alarms:-x}" != "0" || "${unproven:-x}" != "0" ]]; then
    grep -iE 'got status (unknown|invalid)|\[eva:alarm\]' "$log" \
      | grep -viE 'remove-redundant' >&2 || true
    echo "FAIL: eva[$label] is not clean" >&2
    exit 1
  fi
}

run_harness board \
  tools/framac/eva_harness.c src/engine/board/attacks.c src/engine/eval/nnue/nnue_affine.c
run_harness movegen \
  tools/framac/eva_movegen.c src/engine/board/attacks.c
# eva_slider.c #includes attacks.c to reach the file-static Magics[][], so attacks.c is
# NOT passed again here -- doing so would doubly-define it.
run_harness slider \
  tools/framac/eva_slider.c
# The DirtyThreat codec runs under the bitwise+equality domains (extra frama-c flags,
# passed through run_harness's "$@"): its threatened_sq decoder is a shift-and-mask the
# interval domain cannot close without a state blow-up. The domains only add precision, so
# they are scoped to this small harness rather than paid on every target.
run_harness threat \
  -eva-domains bitwise,equality tools/framac/eva_threat.c
run_harness fen \
  tools/framac/eva_fen.c src/engine/board/position.c src/engine/board/attacks.c \
  src/engine/board/zobrist.c src/engine/board/board_props.c src/engine/board/bitboard.c

# eva_numa proves the NumaPolicy string parser reads no byte off the buffer and overflows no
# accumulator, over concrete adversarial inputs. With no case-split intervals it is sound at
# a coarse, cheap setting (~1s) where the codec default takes ~2 min for nothing -- so lower
# precision/slevel here. Keep this LAST: it leaves the two variables changed. eva_numa.c
# #includes numa.c to reach the static parse helpers, so numa.c is NOT passed again.
# -warn-special-float nan: distribute_threads seeds its fill accumulator with +infinity on
# purpose (fc_stubs.h specs the builtin), so permit the infinity comparison while still
# catching a NaN. Scoped to this harness -- the others use no floats and keep the default.
EVA_PRECISION=4
EVA_SLEVEL=200
run_harness numa \
  -warn-special-float nan tools/framac/eva_numa.c
