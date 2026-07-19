# Affine kernel: the nps deficit

Audit finding, recorded so it is not rediscovered. **Correctness is not affected** —
the affine / activation / output-scaling path was checked line by line against
zfish and against upstream and is value-identical. This is purely throughput.

## The measurement

| build | nps |
|---|---|
| ccfish, classical eval | ~843k |
| ccfish, NNUE | ~427k |
| zfish, NNUE | ~956k |

The search is already at parity; the whole deficit is NNUE inference.

## The cause

`nnue_affine.c` accumulates in the interleaved OUT*4 domain. For fc_0 that is
`NnueV128i32` — 128 lanes of `int32_t`, **512 bytes**. The build targets SSE4.1
(`CFLAGS_ARCH` in `build.sh`), which has sixteen 128-bit xmm registers, so the
accumulator needs thirty-two of them. It cannot live in registers, and the whole
thing spills and reloads on every group iteration.

Per group, this shape emits roughly:

- 32x `pmovzxbd` (u8 -> i32)
- 32x `pmovsxbd` (i8 -> i32)
- 32x `pmulld` (SSE4.1, ~10-cycle latency)
- 32x `paddd`, plus ~64 spill loads/stores

zfish's SSSE3 tier does the same group with **8x `pmaddubsw` + 8x `pmaddwd` +
8x `paddd`** into a 128-byte accumulator that stays in registers: the
dot-product reduction happens inside the multiply rather than after a 4x
widening. That is a 6-8x instruction-count difference on fc_0, which dominates
affine cost.

## The fix, in priority order

1. **Adopt the `pmaddubsw` + `pmaddwd` reduction** so the accumulator is 8 xmm
   registers instead of 32. This is the whole win; the rest is noise beside it.
   zfish `engine/eval/nnue_affine.zig` has the tier; upstream has it in
   `nnue/layers/affine_transform_sparse_input.h`.
2. Narrow and store the activation vector once instead of extracting lanes
   scalar-by-scalar (`nnue_affine.c` ClippedReLU and SqrClippedReLU stores).
3. Spell `min`/`max` as the ternary `a < b ? a : b` in `simd.h` rather than the
   bitwise blend `(a & m) | (b & ~m)`. GCC reliably lowers the ternary to
   `pminsd`/`pmaxsd` and matches the blend form far less reliably. The scalar
   fallback in the same header already uses the ternary.
4. `sparse` is a runtime `bool` and the kernels live in a separate TU with no
   LTO, so `OUT` and `sparse` never constant-fold. zfish has both as comptime.

## The constraint

Any change here must leave the node count untouched — `./build.sh signature`
must not move, and the scalar path must stay bit-identical to the vector path.
Integer accumulation is exact int32, so reassociating the adds is safe;
changing a width, a shift, a clamp bound or the fold order is not.
