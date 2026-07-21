# Frama-C: installation and verification

How to install [Frama-C](https://frama-c.com/) and its SMT provers, and how fcfish
uses them — the three analysis gates (`frama-c`, `eva`, `wp`), the analyser setup
that makes the tree parseable, and the division of labour between the Eva and WP
plugins.

Audience: anyone running the verification gates or extending them. The gate table
these three rows live in is in [09-tooling-ci.md](09-tooling-ci.md); the C17 the
tree is written in — and *why* it is C17 rather than C23 — is in
[08-idiomatic-c.md](08-idiomatic-c.md).

Frama-C is **optional infrastructure**. Every gate that needs it exits `127` and is
reported *skipped, not passed*, on a host without the opam switch — so a clone with
no Frama-C still runs `./build.sh parity` green over everything else. The reason the
sources hold to C17 at all is that Frama-C 32.1's `-std c23` frontend rejects
`nullptr`, fixed-underlying-type enums and `[[attributes]]`; C17 is the newest ISO
standard it parses in full.

## Installation

Frama-C is an OCaml program distributed through [opam](https://opam.ocaml.org/), the
OCaml package manager. The WP plugin proves verification conditions by handing them
to external SMT provers, which opam (or a release binary) installs separately and
[Why3](https://www.why3.org/) then registers.

### From scratch on Ubuntu 24.04 (including WSL2)

```sh
# System build dependencies for opam and the OCaml provers.
sudo apt update
sudo apt install -y build-essential curl unzip bubblewrap m4 git libgmp-dev

# opam itself, then a switch. --disable-sandboxing is required under WSL2, where
# bubblewrap's namespaces are not available.
sudo sh -c "$(curl -fsSL https://raw.githubusercontent.com/ocaml/opam/master/shell/install.sh)"
opam init --disable-sandboxing -y
eval $(opam env)

# Frama-C (pulls the kernel, Eva and WP).
opam install frama-c -y
```

Every shell that runs Frama-C needs the switch on its `PATH`; that is what
`eval $(opam env)` does. The gate scripts run it themselves, so a bare
`./build.sh frama-c` works, but an interactive `frama-c …` invocation needs it first.

### The SMT provers (for WP)

The `frama-c` and `eva` gates need no prover — the kernel parses, and Eva is an
abstract interpreter. The `wp` gate is deductive and hands each goal to an SMT
solver. Install at least Z3, which is the one the `wp` gate targets; Alt-Ergo and
CVC5 are useful as alternates that WP can race. **Run `why3 config detect` after
each**, so Why3 writes the prover into `~/.why3.conf` where WP looks for it.

```sh
opam install z3            # the bitvector-strong solver the wp gate uses
why3 config detect         # register it with Why3

opam install alt-ergo      # 2.6.x; the historical Frama-C default, pure OCaml
why3 config detect

# CVC5 ships as a static release binary rather than an opam package.
curl -L -o ~/.local/bin/cvc5 \
  https://github.com/cvc5/cvc5/releases/latest/download/cvc5-Linux-x86_64-static
chmod +x ~/.local/bin/cvc5   # ensure ~/.local/bin is on PATH
why3 config detect
```

Confirm what WP can see:

```sh
frama-c -wp-detect
```

It should list Z3 (and any other prover you registered). If a prover is installed
but missing here, its `why3 config detect` did not run or `~/.why3.conf` is stale —
re-run it.

### Versions and one caveat

The tree is developed against **Frama-C 32.1 (Germanium)**, **Z3 4.16**, and
**Why3 1.8**. Why3 1.8 recognises Alt-Ergo `2.5.4`–`2.6.x`; an older Alt-Ergo (e.g.
`2.4.3`) still runs but Why3 flags it as an unrecognised version and it is slow on
bitvector goals — install `2.6.x` via opam as above. The `wp` gate uses Z3 precisely
because it is the fastest of the three on the arithmetic goals fcfish gives it.

### Uninstalling

```sh
opam remove frama-c -y
# opam switch remove .          # if you made a local switch
opam clean --cache --logs
# And, only if you also want opam and its apt build deps gone:
# sudo rm -f /usr/local/bin/opam && sudo apt remove --purge opam
# sudo apt autoremove --purge
# rm -rf ~/.opam
```

## Using Frama-C in fcfish

Three `./build.sh` steps drive Frama-C. Each is a gate in `parity`; each exits `127`
(→ *skipped*) when frama-c — or, for `wp`, Z3 — is absent.

| Step | Plugin | Proves |
| --- | --- | --- |
| `frama-c` | kernel | the whole `SOURCES` set parses and typechecks as one C17 program to the analyser, not just to clang |
| `eva` | Eva | runtime safety **and** codec correctness of the board-layer pure helpers |
| `wp` | WP + Z3 | the ACSL contracts on the non-bitwise arithmetic helpers, symbolically over every input |

The scripts live in [`../tools/framac/`](../tools/framac/):
[`parse.sh`](../tools/framac/parse.sh), [`eva.sh`](../tools/framac/eva.sh) with its six
harnesses — the board/NNUE [`eva_harness.c`](../tools/framac/eva_harness.c), the movegen
[`eva_movegen.c`](../tools/framac/eva_movegen.c), the slider
[`eva_slider.c`](../tools/framac/eva_slider.c), the DirtyThreat
[`eva_threat.c`](../tools/framac/eva_threat.c), the FEN
[`eva_fen.c`](../tools/framac/eva_fen.c) and the NumaPolicy-parser
[`eva_numa.c`](../tools/framac/eva_numa.c) — and
[`wp.sh`](../tools/framac/wp.sh) with its driver
[`wp_driver.c`](../tools/framac/wp_driver.c).

### The analyser setup

Three choices in the scripts are what make the tree analysable at all; they are the
same for every gate:

- **`-machdep gcc_x86_64`.** It matches the clang/gcc target the engine ships on, so
  the `__builtin_*` bit operations resolve and the 64-byte `alignas(CACHE_LINE_SIZE)`
  is accepted. The default `x86_64` machdep caps alignment at 16 and knows no
  builtins.
- **`-DFCFISH_SIMD_SCALAR`.** The kernel cannot model GCC vector extensions or the
  x86 intrinsic headers, so the analysis takes the scalar SIMD path — the same one
  `./build.sh simd-scalar` proves is bit-identical to the vector path. Only
  `src/engine/eval/nnue/simd.h` differs between the two.
- **The affinity shims.** [`fc_stubs.h`](../tools/framac/fc_stubs.h) is force-included
  to supply the Linux CPU-affinity surface (`cpu_set_t`, the `CPU_*` macros) that
  Frama-C's bundled libc omits, so `src/platform/thread.c` and `numa.c` parse. It is
  seen only by the analyser, never by clang or gcc. It also maps the `__builtin_memcpy` the NNUE SIMD load/store use onto the spec-carrying libc `memcpy`, so Eva tracks initialisation through the copy.

One construct the kernel cannot represent is the 128-bit multiply in
`src/engine/search/tt.c` (`mul_hi64`); it carries a `#ifdef __FRAMAC__` 64-bit
equivalent, compiled only under the analyser and never into the shipped engine.

### Eva versus WP: which helper goes where

Eva and WP prove different things by different means, and fcfish splits the board
helpers between them along one line — **whether the function does bit-twiddling**.

- **Eva** case-splits the input domain and evaluates concretely, so it discharges the
  bitwise codecs (`make_square`/`make_move`/`make_move_typed`/`make_piece` and their
  decoders, all five fields of the `dirty_threat_make` feature word — the middle
  shift-and-mask field under Eva's bitwise domain, see `eva_threat.c` — `shift_bb`, `pawn_attacks_bb`,
  `aligned`, `attacks_bb`'s leaper path and its slider path (the magic-index read, proved in bounds from the shift and block size alone — no `attacks_init`, see `eva_slider.c`), `nnue_clipped_relu_32`, `flip_rank`/`flip_color`, `relative_rank`, and the movegen buffer-write discipline: `make_promotions`/`generate_castling` for all inputs `generate_pawn_moves`/`generate_piece_moves` for representative positions, the FEN parser proved not to write off the board on malformed input, and the NumaPolicy string parser's tokenizer/decimal-parse (`for_each_index`/`parse_element`/`parse_uint`, see `eva_numa.c`) proved to read no byte off the buffer and overflow no accumulator on adversarial input — the CPU-insertion path that follows it grows arrays through `realloc`, which Eva's allocation model cannot track, so that part is left to review and the unit tests): it
  proves both runtime safety — no
  out-of-range shift, signed overflow or out-of-bounds access — and *correctness*,
  that each encoder decodes back exactly.
- **WP** proves ACSL contracts symbolically, over every input at once, and hands the
  goals to Z3. That is stronger where it applies, but WP encodes C shifts and masks as
  nonlinear integer goals that no available SMT prover discharges in reasonable time.
  So WP takes the **non-bitwise** helpers — `piece_value` (no out-of-bounds table
  read, exact value per piece type), `mate_in`/`mated_in` (exact value, score
  bounds), `score_classify` in `src/engine/board/score.c` (a valid classification, no INT32_MIN negation overflow), `stats_update` in `src/engine/search/history.h` (the gravity update keeps
  `|entry| <= D`, so the int16 history storage never overflows), `stat_malus` in
  `src/engine/search/history.c` (the search malus stays `<= 2244`, inside every
  history clamp band, with no overflow), `correction_history_bonus`,
  `prior_bonus_scale` and `prior_scaled_bonus_base` in
  `src/engine/search/search_common.c` (a correction bonus in `[-265, 265]`, a prior
  bonus that never goes negative, a scaled-bonus base `<= 1337`, all overflow-free),
  `depth_saturating_sub` in `src/engine/search/tt.c` (the TT depth clamp is
  `max(depth - n, 0)` and never wraps a shallow entry deep), and `store` in
  `src/shell/ucioption.c` (the untrusted option-string copy never overruns its fixed
  buffer, proved with a loop invariant) — where Z3 closes every goal in
  milliseconds.

The ACSL contracts live as `/*@ … */` comments in the production sources (e.g.
`src/engine/board/types.h`, `src/engine/search/tt.c`). They are invisible to clang,
so the shipped engine is byte-for-byte unchanged whether or not Frama-C is
installed. `wp.sh` analyses `tt.c` only for that one function; its atomic and
128-bit code is never a WP goal.

### Extending the gates

- **A new Eva target:** add a call in `eva_harness.c` that feeds the function its
  full valid interval (with `Frama_C_interval` / `Frama_C_interval_split`), stating
  the function's precondition as the interval it is given. Re-run `./build.sh eva`;
  the gate fails on any alarm or any unproven `//@ assert`.
- **A new WP contract:** write the `/*@ requires/assigns/ensures */` above the
  function, include its header in `wp_driver.c` if not already, and add the function
  name to `FCTS` in `wp.sh`. Re-run `./build.sh wp`; the gate fails unless every goal
  is proved. Keep WP to non-bitwise functions — a shift-heavy contract will time out.

Run all three at once, alongside the rest of the battery, with `./build.sh parity`.
