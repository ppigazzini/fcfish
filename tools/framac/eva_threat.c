// Frama-C Eva harness for the DirtyThreat feature word (dirty_threat_make and its
// decoders in src/engine/board/types.h) -- the NNUE full-threats feature indexer's
// contract: five disjoint bit fields packed into one uint32 and decoded back.
//
// This is split out of eva_harness.c because it runs under Eva's BITWISE domain
// (eva.sh passes -eva-domains bitwise,equality only for this harness). One decoder,
// threatened_sq's `(d >> 8) & 0xFF`, is a shift AND a mask whose masked window sits
// above bit 0; the interval domain cannot prove it recovers the field without splitting
// every higher field that the shift moves into range (a 2*16*16*64 state blow-up), but
// the bitwise domain, which tracks the word bit by bit, closes it with only the decoded
// field split. That domain is too costly to enable on eva_harness.c's larger split
// states (the 64x64 move codec, the 32-lane clipped ReLU), so the DirtyThreat proofs --
// all cheap under it -- live here instead. Run it with tools/framac/eva.sh.

#include "engine/board/types.h"

#include "__fc_builtin.h"

// A Square in [0, 63]; SQ_NONE (64) is not a field value the codec stores.
static Square any_square(void) { return (Square) Frama_C_interval(0, SQ_H8); }

// The flag and the two piece fields (add at bit 31, pc at 20..23, threatened_pc at
// 16..19). Split each and every field above it so their masks isolate; the two square
// fields range free, proved on their own below.
static void check_dirty_threat_codec(void) {
    const int add = Frama_C_interval_split(0, 1);
    const Piece pc = (Piece) Frama_C_interval_split(0, 15);
    const Piece threatened_pc = (Piece) Frama_C_interval_split(0, 15);
    const Square pc_sq = any_square();
    const Square threatened_sq = any_square();
    const uint32_t d = dirty_threat_make((bool) add, pc, threatened_pc, pc_sq, threatened_sq);
    const int back_add = dirty_threat_add(d);
    const Piece back_pc = dirty_threat_pc(d);
    const Piece back_threatened_pc = dirty_threat_threatened_pc(d);
    //@ assert dirty_add_roundtrip: back_add == add;
    //@ assert dirty_pc_roundtrip: back_pc == pc;
    //@ assert dirty_threatened_pc_roundtrip: back_threatened_pc == threatened_pc;
}

// pc_sq is the bottom field (bits 0..7): its decoder is `d & 0xFF`, a mask with no shift,
// so the higher fields contribute only multiples of 256 -- congruence sees d == pc_sq
// (mod 256) and discharges the round-trip with pc_sq alone split, the others ranging
// freely (this one holds under the interval domain too; it is kept here for cohesion).
static void check_dirty_threat_pc_sq(void) {
    const int add = Frama_C_interval(0, 1);
    const Piece pc = (Piece) Frama_C_interval(0, 15);
    const Piece threatened_pc = (Piece) Frama_C_interval(0, 15);
    const Square threatened_sq = any_square();
    const Square pc_sq = (Square) Frama_C_interval_split(0, 63);
    const uint32_t d = dirty_threat_make((bool) add, pc, threatened_pc, pc_sq, threatened_sq);
    const Square back_pc_sq = dirty_threat_pc_sq(d);
    //@ assert dirty_pc_sq_roundtrip: back_pc_sq == pc_sq;
}

// threatened_sq is the middle field (bits 8..15): its decoder is `(d >> 8) & 0xFF`, a
// shift AND a mask, so pc_sq's mod-256 congruence does not reach it -- the interval
// domain cannot see that the free higher fields (bits >= 16, which the shift moves to
// bits >= 8 and the mask then drops) leave bits 8..15 holding exactly threatened_sq. The
// bitwise domain tracks the word bit by bit and sees it, so only threatened_sq is split
// (64 states) and add/pc/threatened_pc/pc_sq range free -- no 2*16*16*64 blow-up, and no
// need to split the low fields. This is the decoder that needs this harness at all.
static void check_dirty_threat_threatened_sq(void) {
    const int add = Frama_C_interval(0, 1);
    const Piece pc = (Piece) Frama_C_interval(0, 15);
    const Piece threatened_pc = (Piece) Frama_C_interval(0, 15);
    const Square pc_sq = any_square();
    const Square threatened_sq = (Square) Frama_C_interval_split(0, 63);
    const uint32_t d = dirty_threat_make((bool) add, pc, threatened_pc, pc_sq, threatened_sq);
    const Square back_threatened_sq = dirty_threat_threatened_sq(d);
    //@ assert dirty_threatened_sq_roundtrip: back_threatened_sq == threatened_sq;
}

int eva_main(void) {
    check_dirty_threat_codec();
    check_dirty_threat_pc_sq();
    check_dirty_threat_threatened_sq();
    return 0;
}
