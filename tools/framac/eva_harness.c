// Frama-C Eva runtime-safety harness for the pure integer helpers of the board layer.
//
// Eva explores from eva_main with every input replaced by its full valid interval, so
// a clean run (no red alarms) is a proof that these helpers commit no runtime error --
// no out-of-range shift, no signed overflow, no out-of-bounds access -- for ANY input
// in the stated domain. The domain each helper requires is expressed by the interval
// it is fed here; that is the point of the harness, not incidental.
//
// This TU needs no other source: every function under test is a `static inline` in the
// two headers, so its body is present. Run it with tools/framac/eva.sh.

#include "engine/board/attacks.h"
#include "engine/board/bitboard.h"
#include "engine/board/types.h"

#include "__fc_builtin.h"

// A Square in [0, 63]. SQ_NONE (64) is deliberately excluded: square_bb(64) would shift
// a 64-bit value by 64, which is undefined -- callers must never index a bitboard by it.
static Square any_square(void) { return (Square) Frama_C_interval(0, SQ_H8); }

// A full 16-bit move pattern. The decoders must be total over every stored bit pattern:
// a Move read back from the transposition table can be any 16-bit value.
static Move any_move(void) { return (Move) Frama_C_unsigned_short_interval(0, 0xFFFF); }

// Square encode/decode round-trips and the rank/file projections.
static void check_square_algebra(void) {
    const int f = Frama_C_interval(0, 7);
    const int r = Frama_C_interval(0, 7);
    const Square s = make_square(f, r);
    (void) rank_of(s);
    (void) file_of(s);
    (void) flip_rank(s);
    (void) relative_rank(WHITE, s);
    (void) relative_rank(BLACK, s);
}

// Piece encode/decode over the valid colour x type product.
static void check_piece_algebra(void) {
    const Color c = (Color) Frama_C_interval(WHITE, BLACK);
    const PieceType pt = (PieceType) Frama_C_interval(NO_PIECE_TYPE, KING);
    const Piece pc = make_piece(c, pt);
    (void) color_of_piece(pc);
    (void) type_of_piece(pc);
    (void) flip_color(c);
    (void) piece_value((PieceType) Frama_C_interval(PAWN, KING));
}

// Move encode/decode. Encoding takes two distinct valid squares; decoding must accept
// any 16-bit pattern.
static void check_move_codec(void) {
    const Square from = any_square();
    const Square to = any_square();
    const Move m = make_move(from, to);
    (void) move_from(m);
    (void) move_to(m);
    (void) move_is_ok(m);

    const MoveType t = (MoveType) Frama_C_interval(NORMAL, CASTLING);
    const PieceType promo = (PieceType) Frama_C_interval(KNIGHT, QUEEN);
    const Move mt = make_move_typed(t, from, to, promo);
    (void) mt;

    const Move raw = any_move();
    (void) move_from(raw);
    (void) move_to(raw);
    (void) move_type(raw);
    (void) move_promotion(raw);
    (void) move_is_ok(raw);
}

// Bitboard single-square constructors and membership, over the valid square domain.
static void check_bitboard_squares(void) {
    const Square s = any_square();
    const Bitboard bs = square_bb(s);
    (void) file_bb(Frama_C_interval(0, 7));
    (void) rank_bb(Frama_C_interval(0, 7));
    (void) bb_test(bs, s);
    (void) bb_more_than_one(bs);
    (void) popcount_bb(bs);
}

// Scan primitives. lsb/msb/pop_lsb rest on __builtin_ctzll/clzll, undefined at zero, so
// they take a strictly non-empty board.
static void check_bitboard_scan(void) {
    Bitboard b = Frama_C_unsigned_long_long_interval(1, 0xFFFFFFFFFFFFFFFFULL);
    (void) lsb(b);
    (void) msb(b);
    (void) pop_lsb(&b);
}

// Directional shifts and pawn attacks over an arbitrary board. shift_bb's shift widths
// (8, 1, 9, 7) are all below 64, and its edge masks keep the wrap-off bits from ever
// re-entering; both are total over every 64-bit board and every direction.
static void check_bitboard_shifts(void) {
    const Bitboard b = Frama_C_unsigned_long_long_interval(0, 0xFFFFFFFFFFFFFFFFULL);
    const Direction dirs[8] = { NORTH,      EAST,       SOUTH,      WEST,
                                NORTH_EAST, SOUTH_EAST, SOUTH_WEST, NORTH_WEST };
    for (int i = 0; i < 8; ++i)
        (void) shift_bb(dirs[i], b);
    (void) pawn_attacks_bb(WHITE, b);
    (void) pawn_attacks_bb(BLACK, b);
}

// Collinearity test. aligned indexes LineBB[s1][s2] -- so s1, s2 must be valid squares
// -- and shifts 1 << s3, so s3 must be below 64. The table content is irrelevant to
// runtime safety, which is why this needs LineBB defined (attacks.c is linked) but not
// filled by attacks_init.
static void check_alignment(void) {
    const Square s1 = any_square();
    const Square s2 = any_square();
    const Square s3 = any_square();
    (void) aligned(s1, s2, s3);
}

// Beyond safety: prove the encode/decode codecs are CORRECT, not merely alarm-free.
// Frama_C_interval_split forces Eva to evaluate each concrete input in its own state, so
// it discharges the exact round-trip relations that its (non-relational) interval domain
// cannot see across a whole range at once. ACSL cannot call C functions, so each decoded
// value is bound to a local first and the assertion compares those locals.
static void check_codec_correctness(void) {
    // Square: make_square then file_of/rank_of recovers the file and rank, over all 64
    // squares.
    const int f = Frama_C_interval_split(0, 7);
    const int r = Frama_C_interval_split(0, 7);
    const Square s = make_square(f, r);
    const int back_f = file_of(s);
    const int back_r = rank_of(s);
    //@ assert square_file_roundtrip: back_f == f;
    //@ assert square_rank_roundtrip: back_r == r;

    // Piece: make_piece then color_of_piece/type_of_piece recovers both fields.
    const Color c = (Color) Frama_C_interval_split(WHITE, BLACK);
    const PieceType pt = (PieceType) Frama_C_interval_split(PAWN, KING);
    const Piece pc = make_piece(c, pt);
    const int back_c = color_of_piece(pc);
    const int back_pt = type_of_piece(pc);
    //@ assert piece_color_roundtrip: back_c == c;
    //@ assert piece_type_roundtrip: back_pt == pt;

    // Move: make_move then move_from/move_to recovers both squares, over all 64x64 pairs.
    const Square from = (Square) Frama_C_interval_split(0, 63);
    const Square to = (Square) Frama_C_interval_split(0, 63);
    const Move m = make_move(from, to);
    const Square back_from = move_from(m);
    const Square back_to = move_to(m);
    //@ assert move_from_roundtrip: back_from == from;
    //@ assert move_to_roundtrip: back_to == to;
}

// The typed move packs four disjoint bit fields: type<<14 | (promo-KNIGHT)<<12 |
// from<<6 | to. Prove the two fields the plain codec above does not exercise -- type and
// promotion -- recover exactly. Only type and promo are case-split (16 states); from and
// to range over their full intervals, because move_type/move_promotion mask them off, and
// their own round-trip is the make_move proof above (same bit positions 0..11).
static void check_typed_move_codec(void) {
    const MoveType t = (MoveType) Frama_C_interval_split(NORMAL, CASTLING);
    const PieceType promo = (PieceType) Frama_C_interval_split(KNIGHT, QUEEN);
    const Square from = any_square();
    const Square to = any_square();
    const Move m = make_move_typed(t, from, to, promo);
    const MoveType back_t = move_type(m);
    const PieceType back_promo = move_promotion(m);
    //@ assert typed_type_roundtrip: back_t == t;
    //@ assert typed_promo_roundtrip: back_promo == promo;
}

int eva_main(void) {
    check_square_algebra();
    check_piece_algebra();
    check_move_codec();
    check_bitboard_squares();
    check_bitboard_scan();
    check_bitboard_shifts();
    check_alignment();
    check_codec_correctness();
    check_typed_move_codec();
    return 0;
}
