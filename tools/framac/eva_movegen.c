// Frama-C Eva harness for movegen's buffer-write discipline.
//
// The generators append moves through `(list++)->move = ...`, and movegen.h's contract is
// that LIST has room for MAX_MOVES entries. This harness proves, for the two generators
// whose write count is bounded WITHOUT a filled attack table or a concrete position, that
// they never advance past their buffer:
//
//   make_promotions   -- at most 4 writes (one queen plus the three minor promotions),
//                        proved over every GenType/enemy/from/to.
//   generate_castling -- a fixed two-side loop that writes at most one move per side,
//                        so at most 2, proved over every castling-rights value.
//
// Each is given a buffer sized to its exact bound, so any extra write would be an
// out-of-bounds alarm; 0 alarms plus the `end - buf` assertion is the proof.
//
// It #includes movegen.c so the static generators are in scope; Eva analyses only what
// eva_main reaches, so generate()/generate_pawn_moves/generate_piece_moves (which loop
// over a position's bitboards and, for sliders, need attacks_init's magic tables) are
// parsed but not explored -- their buffer bound rests on the chess move-count fact and
// waits on a valid-Position / attacks-init model.
#include "engine/board/movegen.c"

#include "__fc_builtin.h"

// make_promotions writes at most the queen plus three minor promotions.
static void check_make_promotions(void) {
    ExtMove buf[4];
    const Square from = (Square) Frama_C_interval(0, 63);
    const Square to = (Square) Frama_C_interval(0, 63);
    const GenType type = (GenType) Frama_C_interval(GEN_CAPTURES, GEN_NON_EVASIONS);
    const bool enemy = (bool) Frama_C_interval(0, 1);
    const ExtMove *end = make_promotions(buf, to, from, type, enemy);
    //@ assert promo_within: 0 <= end - buf <= 4;
}

// generate_castling loops over the two sides and writes at most one move per side. A king
// on e1 with an all-but-the-king-empty board leaves both castling paths clear, so the
// arbitrary rights drive Eva through every write/skip combination; the bound holds for all.
static void check_generate_castling(void) {
    Position pos;
    StateInfo st;
    pos.st = &st;
    st.castling_rights = (CastlingRights) Frama_C_interval(NO_CASTLING, ANY_CASTLING);
    pos.by_type[KING] = square_bb(SQ_E1);
    pos.by_color[WHITE] = square_bb(SQ_E1);
    pos.by_type[ALL_PIECES] = square_bb(SQ_E1);
    for (int i = 0; i < 16; i++)
        pos.castling_rook_square[i] = (Square) Frama_C_interval(0, 63);
    ExtMove buf[2];
    const ExtMove *end = generate_castling(&pos, buf, WHITE);
    //@ assert castling_within: 0 <= end - buf <= 2;
}

int eva_main(void) {
    check_make_promotions();
    check_generate_castling();
    return 0;
}
