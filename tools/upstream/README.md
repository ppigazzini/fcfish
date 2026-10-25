# Upstream tracking

fcfish targets a **bit-exact 1:1 clone of Stockfish**. This directory holds the
state and the tooling that makes that checkable.

## The repos

| Role | Path | Use |
|---|---|---|
| **Golden** | `../Stockfish` | the definition of correct behaviour |
| **Sibling** | `../zfish` | an independent Zig port of the same golden |
| **Sibling** | `../rfish` | an independent Rust port of the same golden |
| **Sibling** | `../fcfish` | an independent C17 port of the same golden |

fcfish also has a **port source**, `../mcfish` — its C23 sibling, the same engine
one language revision apart. It is not a reference for behaviour, so it is not in
the table; [PORT_SOURCES.md](PORT_SOURCES.md) says how it is tracked.

**Only one of these is an authority.** The differential gates compare fcfish
against a **pristine upstream build**, never against a sibling. Where fcfish and
Stockfish disagree, Stockfish wins; where a sibling and Stockfish disagree, that
is a bug report for the sibling.

### A sibling finding is a hypothesis, not a bug report

Every rule below is written about zfish because that is where the pattern was
paid for; each applies unchanged to `../rfish` and `../fcfish`. One rule the
rfish sweeps added: **probe the named behaviour against the oracle before writing
a fix.** Of the eleven behaviours the fourth rfish sweep named, seven were already
correct here — the four that were not became `2ccd9c5a`, `d71ad444`, `8415fd6b`
and `f4f10372`, and the seven that were are recorded in those commit bodies so
the next sweep does not re-probe them. `git log --grep=rfish` finds the set.

### zfish is a sibling, not a source

fcfish began as a port OF zfish — a complete, bit-exact Zig port of Stockfish,
whose C++ templates, classes, RAII and operator overloading were already gone, so
Zig → C17 was close to mechanical. **That relationship is over.** fcfish now ports
from mcfish, and mcfish and zfish are peer ports of the same golden, each syncing
to Stockfish on its own schedule. The rules below are mcfish's, and fcfish holds
to them through it:

- **Neither tree is behind the other.** There is no commit range to absorb, so
  there is no pin here for zfish and `sync-status` does not mention it. Most of
  either log is language work — Zig std churn on one side, C idiom on the other
  — that will never have a counterpart. A status line calling that a deficit is a
  false alarm by construction. zfish does not pin fcfish either; it tracks
  Stockfish and refers to this tree as "the sibling C port" in prose.
- **Sweeps are bidirectional, and the direction is not obvious from the log.**
  The two cross-port constantly. In the 2026-08-01 sweep the numa insert, the
  `NumaPolicy` parse and the `setoption` grammar all turned out to flow
  mcfish → zfish — one of those zfish commits says in its own body that it was
  found by auditing mcfish. Ask what the sibling has that this tree should take
  AND what this tree has that the sibling should; answer both from the code.
- **Checking a SHA against the other tree's log is NOT the test.** Both reach the
  same wins independently and neither cites the other reliably: zfish `39bc445d`
  credits mcfish in its subject, while PEXT in `attacks.c` and the AVX2 maddubs
  tier in `simd.h` sit here plainly with no citation at all. Read the code.
- **A measurement does not transfer, in either direction.** This is symmetric and
  it is the trap that has cost the most time. The C ports' accumulator row tile is 64
  where zfish's is 128/256 (`f514ac56`, `2474792e`) because mcfish swept it and 64
  won. zfish's runBack inline won 1.0% there and measured FLAT in mcfish,
  because its gain was Zig's missing TBAA forcing reloads that clang+LTO never
  paid. A width or an inline is tuned for the language, tier and tree it was
  measured on. Re-measure or do not take it.
- **Refutations are the durable output of a sweep, and the commit log is where they
  live.** Each one is recorded in the body of the mcfish commit that measured it and
  refused it, ported here with it, so `git log --grep=zfish --grep=rfish` is the ledger — searchable, and
  it moves with the tree. Already refuted in mcfish: zfish `3d1ea031` runBack inline,
  `7b8a8b10` the THP skip, `0bfbf31c` the contiguous layer arena, the movepick
  pawn-history hoist, the fused refresh threat pass. `e15c4565` was reverted in
  zfish itself (`bb6fd153`). Search before re-deriving any of them.

There is no bookmark file for "how far the sibling has been read". A peer sweep
compares the two trees as they stand rather than replaying a range, and `git log
--grep=zfish` finds what the last one took. The 2026-08-01 sweep read
`f2299c47f..03c14b860` and landed the oracle golden audit and the tree-wide
`.gitattributes` rule; the 2026-07-24 audit closed `6a4a80887..f2299c47f`.

## State files

- **`UPSTREAM_BASE`** — the Stockfish SHA fcfish is porting to. The bench count of
  *this commit* is the finish line. Advance it only when `upstream-parity` is green.
- **`UPSTREAM_TARGET`** — the SHA being ported toward when catching up to a moving
  upstream; equal to `UPSTREAM_BASE` when synced.

**`./build.sh sync-status` is what checks these**, comparing the pin to the golden
checkout's `HEAD` and listing every commit in between, in BOTH directions — a
checkout sitting *before* the pin is red, because every grep of it then answers
from source this tree has already ported past. It reports; it does not gate,
because upstream moving is normal — the failure it catches is not noticing.
Before this step existed nothing read the pins at all, and one sat five commits
stale while reading as authoritative.

## Tools

| script | what it does |
|---|---|
| `upstream_oracle.sh [sha]` | builds **pristine** upstream at `sha` in a detached worktree and prints the binary path |
| `upstream_parity.sh [bin] [sha]` | the finish-line gate: fcfish bench vs the oracle bench. Run via `./build.sh upstream-parity`. |
| `mcfish_port.py <command>` | ports mcfish's history onto this branch, one gated commit per mcfish commit — see below |

## Porting from mcfish

mcfish is this tree's C23 sibling, so porting it is a 3-way merge, not a
translation. [`mcfish_port.py`](mcfish_port.py) lowers each mcfish commit to C17
and keeps the result as a commit chain under `refs/mirror/mcfish`; a
cherry-pick of one of those commits merges `lower(parent)` → `lower(commit)` into
this tree, so fcfish's own work — the ACSL contracts, the Frama-C gates, its pages
— rides through every pick, and only a hunk both trees touched conflicts.

```sh
export CLANG_FORMAT=<a clang-format of the pinned major>   # see ./build.sh fmt
python3 tools/upstream/mcfish_port.py mirror        # base: the newest "Port mcfish <sha>." on HEAD
python3 tools/upstream/mcfish_port.py replay        # pick, gate and commit until something stops it
python3 tools/upstream/mcfish_port.py conflicts docs/10-tooling-ci.md
python3 tools/upstream/mcfish_port.py resume --note "why the resolution reads as it does"
python3 tools/upstream/mcfish_port.py skip --reason "C17 still needs <stdbool.h>"
```

**Run it on the branch that receives the ports, from the repository root.** Each
pick becomes one commit carrying mcfish's author, date, subject and body, a
`Port mcfish <sha>.` line, the lowering rules the change needed, any `--note`, and
the gate lines this tree ran — kept apart from mcfish's own evidence, which records
mcfish's run. A pick with nothing left after lowering, and one dropped with
`skip`, still lands as an empty commit saying why, so the log answers "was this
taken?" for every mcfish commit.

**The lowering** spells each C23 construct the way this tree already does:
`nullptr` → `NULL`, a fixed-underlying-type enum → a plain one (`packed` where it
is typedef'd, so its width and every layout over it hold), `[[attr]]` →
`__attribute__((attr))`, `constexpr` → `const`, `<stdbit.h>`'s counts → the
builtins. It also renames `mcfish` to `fcfish`, renames the workflows, and
renumbers the pages past tooling-ci, because the Frama-C page sits after it here
and nowhere in mcfish. Two rules are not equivalent and each has a gate that
sees it: a C17 `const` is not a constant expression (the frama-c gate refuses a
`static_assert` over one), and gcc does not accept `(void)` as discarding a
`warn_unused_result` value (the gcc build refuses it). `lower_c`'s docstring says
how to spell each instead.

**The gates per pick** run only where the pick can move them: the touched C files
are formatted with the pinned clang-format major; `build` and `signature` run on
any change to the sources, `build.sh` or the anchor, and the anchor checked is the
one the pick carries, so it is mcfish's own number at that commit. Inside an
upstream range mcfish moves the bench commit by commit but re-derives the golden
only at the pin advance, so a drift is accepted when the bench moved to a number
the mcfish message itself states — upstream's `Bench:` at that point — or held
still on a pick that states no other number; a bench that stays put while the message names a
new number stops the replay; `docs-lint` and
`cite-check` run on a page change, `shellcheck` on a shell-script change; `frama-c` and `wp` run when the pick lands in a file with
ACSL or adds a construct the kernel refuses; a gcc-14 build into
`build/fcfish-gcc` runs when it adds one the compilers diagnose differently. A
warning is logged to the state directory, not fatal — mcfish's own intermediate
commits carry some — so check the log at a pin boundary. `eva`, `test` and the
rest of `parity` do not run per pick: run `./build.sh parity` at each upstream pin
advance and before calling the replay done.

**Two stop classes resolve themselves.** fcfish's three Frama-C gates sit where
mcfish keeps adding its own: their step functions end where mcfish's next
`do_<step>` lands in `build.sh`, this tool's own `tools-smoke` entry ends where
mcfish appends the next tool it smokes (the roster count is restated), and on the
tooling page they sit inside the gate
table, the `parity` list and its gate count, and the CI section. `replay`
resolves exactly those shapes, recounting the gates from `do_parity`. And a file the pick touched that spells
`bool`, `true`, `false` or `NULL` with no header behind it — C23 keywords, C17
macros, and mcfish dropped `<stdbool.h>` as a no-op — gets the header, is rebuilt,
and the commit body says so. Every other conflict or build failure stops the
replay with the pick staged.

**What no gate sees is the prose.** Renaming the project makes a mcfish sentence
*about* mcfish — a sweep it ran, a figure it measured, a commit of its own — read
as a claim about this tree. Review every ported line that names a sibling, a
measurement or a C23 feature, and attribute the fact to mcfish where it is
mcfish's.

State — the last pick consumed, the staged one, the warning log, the gate logs —
lives in the worktree's git directory under `mcfish-port/`, never in the tree, so
a fresh shell resumes where the last run stopped. Rebuilding the mirror resets it
to the new base.

## Why the oracle is a separate worktree

The oracle must be upstream's own code built by upstream's own Makefile, with no
fcfish edit anywhere near it. If the two shared a tree, a bug present in both
would cancel out and the gate would pass on a wrong engine. The worktree lives
outside the repo (`../.fcfish-upstream-oracle`) and is never committed.

## Why `upstream-parity` is not in `parity`

It needs a network fetch and a full pristine upstream build, which `./build.sh
parity` deliberately does not — so the finish-line gate is a separate step you run
deliberately.

Day to day, `./build.sh signature` is the anchor: it pins fcfish's behaviour so a
refactor cannot change it silently.
