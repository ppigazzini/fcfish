# The three trees, pinned

ccfish is a C23 port. It has two reference trees, and they answer different
questions — so both are pinned here, and neither is optional.

| tree | role | commit |
| --- | --- | --- |
| `../Stockfish` | **golden.** Where zfish and ccfish disagree, this wins. | `86beb0f5406b5e4c110c1890ec88c4a86ff6103a` |
| `../zfish` | **port source.** A Zig port that is already bit-exact; templates, classes and RAII are already gone, which is why ccfish ports from here rather than from C++. | `52bc5c33dc35a11b964e775394c2afa1f35ea21f` |

`tools/upstream/UPSTREAM_BASE` carries the Stockfish sha on its own, because
`upstream_oracle.sh` reads it. This file is the whole picture.

## The two ports are pinned to different upstream commits

zfish's own `tools/upstream/UPSTREAM_BASE` is `9a8dd81dd7f98cbf02f16c59b4377d174d6eb4b5`,
nine commits behind ccfish's. That is not a discrepancy to reconcile — it is a fact
to account for when zfish and ccfish disagree, because the difference may be
upstream's, not the port's.

Those nine commits are, newest first:

```
86beb0f54 Remove some dead fused code
3e3ee33a7 Simplify HalfKAv2_hm::write_indices()
151e204cc Simplify bitboards
1c384d3a8 AVX2 fused SqrClippedReLU + ClippedReLU
4c78ba892 AVX-512 squared clipped ReLU
8ddea7291 Add -fno-ipa-cp-clone for mingw
a255ad59e Optimize evasions in Position::pseudo_legal()
057e9bf4d Fix an MSVC debug break
ca11d4370 Remove dead code left over from earlier refactors
```

**None of them carries a `Bench:` line**, so none moves the node count — which is
why both ports bench the same number despite the gap. Verify rather than assume it
stays that way:

```sh
git -C ../Stockfish log --format='%b' <zfish-base>..<ccfish-base> | grep '^Bench:' | sort -u
```

A non-empty result means the two ports no longer share a signature, and any
ccfish-vs-zfish node comparison is void until one of them moves.

Two of the nine are worth naming because they touch code ccfish has ported and
zfish has not: `a255ad59e` (evasions in `pseudo_legal`) and `1c384d3a8` /
`4c78ba892` (the fused ReLU kernels). A zfish/ccfish difference in those places is
expected.

## Rebuilding the oracle

`tools/upstream/upstream_oracle.sh` checks the golden out into a detached worktree
and builds it. Pass `--verify` — it asserts the built binary benches the commit's
own declared `Bench:`, and without it a stale or locally-edited worktree benches
wrong and every number taken afterwards is fiction.

The oracle is built at `ARCH=x86-64-sse41-popcnt` with the default compiler. That
is correct for **node counts**, which are compiler-independent. It is wrong for any
**instruction or cost ratio**: comparing a gcc-built oracle against a clang-built
ccfish measures the compilers. For that, build the reference with the same
toolchain, and see `CCFISH_ARCH` in `build.sh` for holding the ISA tier constant too.
