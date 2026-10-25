# The three trees, pinned

fcfish is a C17 port. It has two reference trees, and they answer different
questions — so both are pinned here, and neither is optional.

| tree | role | commit |
| --- | --- | --- |
| `../Stockfish` | **golden.** Where a port and Stockfish disagree, this wins. | `f4bcd40409f94bd397a083c5d6243bac6dcc6d85` |
| `../zfish` | **port source.** A Zig port that is already bit-exact; templates, classes and RAII are already gone, which is why fcfish ports from a port rather than from C++. | `f2299c47f35d522a4df7c20d78e037d77c419e53` |
| `../mcfish` | **port source.** The C23 sibling of this tree — same engine, same decomposition, one language revision apart. Most of what lands here now arrives through it, and `ZFISH_BASE` moves behind its audit of the zfish range. | tracked by the `Port mcfish <sha>.` line each port's body opens with, not by a pin file; [`mcfish_port.py`](mcfish_port.py) takes its next base from the newest one |

`tools/upstream/UPSTREAM_BASE` carries the Stockfish sha on its own, because
`upstream_oracle.sh` reads it. This file is the whole picture.

## Keeping the ports level

All three sit at Stockfish `f4bcd404` (SFNNv16), so they share a signature --
upstream's own `Bench:` at that commit. Run `./build.sh signature` for the value.

That will drift again. When it does, the question is not how far apart they are
but whether a **bench-mover** sits between them:

```sh
git -C ../Stockfish log --format='%b' <zfish-base>..<fcfish-base> | grep '^Bench:' | sort -u
```

A non-empty result means the two no longer share a signature, and any
fcfish-vs-zfish node comparison is void until one of them moves.

## Rebuilding the oracle

`tools/upstream/upstream_oracle.sh` checks the golden out into a detached worktree
and builds it. Pass `--verify` — it asserts the built binary benches the commit's
own declared `Bench:`, and without it a stale or locally-edited worktree benches
wrong and every number taken afterwards is fiction.

The oracle is built at `ARCH=x86-64-sse41-popcnt` with the default compiler. That
is correct for **node counts**, which are compiler-independent. It is wrong for any
**instruction or cost ratio**: comparing a gcc-built oracle against a clang-built
fcfish measures the compilers. For that, build the reference with the same
toolchain, and see `FCFISH_ARCH` in `build.sh` for holding the ISA tier constant too.
