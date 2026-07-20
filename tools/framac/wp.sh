#!/usr/bin/env bash
# Deductively prove the non-bitwise pure helpers with Frama-C's WP plugin and Z3.
#
# Where the Eva gate enumerates inputs, WP proves the ACSL contracts SYMBOLICALLY --
# over every input at once -- discharging each function's requires/assigns/ensures plus
# the -wp-rte runtime-safety obligations (no overflow, no out-of-bounds array read). It
# is restricted with -wp-fct to the contracted, non-bitwise helpers: WP encodes C shifts
# as nonlinear integer functions that no available SMT prover discharges in reasonable
# time, so the bitwise codec/bitboard helpers stay with the Eva gate (which case-splits
# them) and WP takes the arithmetic ones, where Z3 is instant.
#
# The contracts live in the production headers as /*@ */ comments -- invisible to clang,
# so the shipped engine is untouched. tools/framac/wp_driver.c is the TU that gives WP
# their bodies.
set -euo pipefail

cd "$(dirname "$0")/../.."

if command -v opam > /dev/null 2>&1; then
  eval "$(opam env)"
fi
if ! command -v frama-c > /dev/null 2>&1; then
  echo "frama-c not found -- activate the opam switch (eval \$(opam env)) or install it." >&2
  exit 127
fi
# WP needs an SMT prover registered with Why3. Z3 is the one this gate targets; without
# it, treat the gate as a SKIP (exit 127) rather than a failure, as the other gates do
# for a missing tool.
if ! frama-c -wp-detect 2> /dev/null | grep -qi 'Z3'; then
  echo "Z3 not detected by WP -- install z3 and run 'why3 config detect'." >&2
  exit 127
fi

# The contracted, WP-tractable helpers. Keep in step with the /*@ */ contracts in
# their sources; a function named here without a contract proves vacuously.
#   piece_value, mate_in, mated_in        -- src/engine/board/types.h
#   stats_update                          -- src/engine/search/history.h
#   depth_saturating_sub                  -- src/engine/search/tt.c
FCTS=piece_value,mate_in,mated_in,stats_update,depth_saturating_sub

# The TUs holding the contracted functions. tt.c is analysed only for the -wp-fct
# function above; its atomic and 128-bit code is never a WP goal.
WP_SOURCES=(tools/framac/wp_driver.c src/engine/search/tt.c)

log=$(mktemp)
trap 'rm -f "$log"' EXIT

# -wp-rte adds the runtime-safety obligations; -wp-timeout 10 is ample for these
# linear/array goals (most close by Qed alone). WP exits 0 even with unproved goals, so
# the pass/fail signal is the "Proved goals: X / Y" summary, checked below.
if ! frama-c \
  -std c17 \
  -machdep gcc_x86_64 \
  -cpp-extra-args="-Isrc -D_POSIX_C_SOURCE=200809L -DFCFISH_SIMD_SCALAR" \
  "${WP_SOURCES[@]}" \
  -wp -wp-rte -wp-prover z3 -wp-timeout 10 -wp-fct "$FCTS" \
  > "$log" 2>&1; then
  cat "$log"
  echo "wp: frama-c aborted" >&2
  exit 1
fi

line=$(grep -E 'Proved goals:' "$log" | tail -1 || true)
proved=$(echo "$line" | grep -oE '[0-9]+ / [0-9]+' | awk '{print $1}')
total=$(echo "$line" | grep -oE '[0-9]+ / [0-9]+' | awk '{print $3}')
echo "wp: ${proved:-?} / ${total:-?} goals proved (z3, functions: $FCTS)"

if [[ -z "${total:-}" || "$proved" != "$total" ]]; then
  echo "--- unproved goals ---" >&2
  grep -iE 'Timeout|Unsuccess|Unknown|Failed' "$log" >&2 || true
  echo "FAIL: WP did not prove every goal" >&2
  exit 1
fi
