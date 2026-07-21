// Frama-C Eva harness for the slider arm of attacks_bb -- the one gap
// eva_harness.c (check_leaper_attacks) and eva_movegen.c both declare "waits on
// modelling attacks_init".
//
// It does not. attacks_bb's ROOK/BISHOP arm reads
//   m->attacks[magic_index(m, occupied)],
//   magic_index = (unsigned) (((occupied & m->mask) * m->magic) >> m->shift).
// The index is in bounds for ANY magic value, because the SHIFT alone bounds it: a
// uint64 right-shifted by `shift` is at most 2^(64-shift) - 1, and attacks_init
// sizes each square's block to exactly 2^popcount(mask) = 2^(64-shift) entries
// (attacks.c:16 sizing, attacks.c:109 shift). So the read safety is a property of
// the shift/size invariant, NOT of the magic search -- and a hand-built Magic entry
// that honours `shift = 64 - popcount(mask)` with a matching block discharges it,
// exactly as eva_movegen.c hand-builds PseudoAttacks instead of running the init.
//
// This TU #includes attacks.c to reach the file-static Magics[][], magic_index and
// magic_slot; attacks_init is never called. Two real corner masks are used, the two
// popcount extremes so the bound is exercised at its tightest and its loosest:
//   a1 rook   mask popcount 12 -> shift 52 -> block 2^12 = 4096 (the largest block),
//   a1 bishop mask popcount  6 -> shift 58 -> block 2^6  =   64.
// The occupied bitboard ranges over the full 64-bit interval. Zero alarms proves
// magic_index never indexes past the block for any occupancy. The QUEEN arm is
// left out on purpose: it is `attacks_bb(BISHOP, s, occ) | attacks_bb(ROOK, s,
// occ)` (attacks.c:157), a disjunction of the two arms proved here that reads no
// memory of its own -- so its safety follows, and driving it would only make Eva
// re-enter attacks_bb recursively, which it models through a (missing) spec.
#include "engine/board/attacks.c"

#include "__fc_builtin.h"

// a1 rook: shift 52 -> 2^(64-52) = 4096 entries. a1 bishop: shift 58 -> 2^6 = 64.
static Bitboard rook_a1_block[4096];
static Bitboard bishop_a1_block[64];

// The a1 relevant-occupancy masks attacks_init would derive (edges excluded):
//   rook a1:   b1..g1 and a2..a7                       -- popcount 12.
//   bishop a1: b2,c3,d4,e5,f6,g7 (h8 is an edge, out)  -- popcount 6.
// Their exact bits are immaterial to the bound; only their popcount fixes the shift.
enum { RookA1Mask = 0x000101010101017EULL, BishopA1Mask = 0x0040201008040200ULL };

static Bitboard any_occupancy(void) {
    return Frama_C_unsigned_long_long_interval(0, 0xFFFFFFFFFFFFFFFFULL);
}

// Build the a1 rook and bishop magic entries by hand, honouring init's invariant
// shift = 64 - popcount(mask) with a block sized 2^popcount(mask). The magic is an
// arbitrary sparse constant: any value keeps the read in bounds, since the shift,
// not the magic, caps the index.
static void setup_a1_magics(void) {
    Magic *r = &Magics[SQ_A1][magic_slot(ROOK)];
    r->mask = (Bitboard) RookA1Mask;
    r->shift = 52;
    r->magic = 0x0080001020400080ULL;
    r->attacks = rook_a1_block;

    Magic *b = &Magics[SQ_A1][magic_slot(BISHOP)];
    b->mask = (Bitboard) BishopA1Mask;
    b->shift = 58;
    b->magic = 0x0002020202020200ULL;
    b->attacks = bishop_a1_block;
}

int eva_main(void) {
    setup_a1_magics();

    // Each arm over an arbitrary occupancy. The rook block is the 4096-entry maximum,
    // so `>> 52` at its widest; the bishop block is 64, so `>> 58`.
    (void) attacks_bb(ROOK, SQ_A1, any_occupancy());
    (void) attacks_bb(BISHOP, SQ_A1, any_occupancy());
    return 0;
}
