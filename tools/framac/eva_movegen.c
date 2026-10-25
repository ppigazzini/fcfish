// Frama-C Eva harness for movegen's buffer-write discipline.
//
// The generators append moves through `(list++)->move = ...`, and movegen.h's contract is
// that LIST has room for MAX_MOVES entries. This harness proves those writes never overrun
// the buffer, at two levels of generality:
//
//   FOR ALL INPUTS -- the generators whose write count is bounded without an attack table
//   or a concrete position:
//     make_promotions    at most 4 writes (queen plus the three minor promotions).
//     generate_castling  a fixed two-side loop, at most one move per side, so at most 2.
//
//   FOR REPRESENTATIVE POSITIONS -- generate_pawn_moves loops over the position's pawn
//   bitboards, so Eva needs a concrete pawn placement to bound the loops (a symbolic
//   bitboard has an unbounded pop_lsb loop). It is magic-free (pawns and en passant both
//   use shift_bb), so no attacks_init is needed. Every generator reads the king square and
//   the pin set, so each position carries a white king and pins nothing; the pin masks then
//   never narrow a set, and the zero-initialised DiagBB/AntiDiagBB they read stay unused.
//   An evasion stage with an all-ones target emits every pawn move, so it stands for the
//   captures and the quiets together. The positions below exercise every branch -- push,
//   double push, promotion, capture-promotion, plain capture, en passant -- each into a
//   buffer sized so any extra write is an alarm.
//
// generate_piece_moves is covered through its LEAPER path (below): its write loop --
// `while (b) (list++)->move = make_move(from, pop_lsb(&b))` -- is identical for every
// piece type, differing only in where the attack bitboard `b` comes from, so proving it
// with a concrete attack set stands for the slider case too. The attack set is supplied by
// hand because attacks_init is not run; that is why this proves the write discipline, not
// the attack values. Only the free-piece loop runs: the pinned loop is reached by sliders
// alone, so it falls under the limit below.
//
// NOT COVERED: the top-level generate/generate_legal as a whole. Running them exercises
// the slider path through attacks_bb, which reads the magic tables attacks_init fills, and
// Eva cannot run that init (its magic search trips out-of-bounds alarms). A general
// generate() <= MAX_MOVES bound also rests on the chess move-count fact (~218 max), which
// needs valid-position invariants formalised -- both are a separate, larger effort.
#include "engine/board/movegen.c"

#include "__fc_builtin.h"

// make_promotions writes at most the queen plus three minor promotions, over all inputs.
static void check_make_promotions(void) {
    ExtMove buf[4];
    const Square from = (Square) Frama_C_interval(0, 63);
    const Square to = (Square) Frama_C_interval(0, 63);
    const GenType type = (GenType) Frama_C_interval(GEN_CAPTURES, GEN_EVASIONS);
    const bool enemy = (bool) Frama_C_interval(0, 1);
    const ExtMove *end = make_promotions(buf, to, from, type, enemy);
    //@ assert promo_within: 0 <= end - buf <= 4;
}

// generate_castling loops over the two sides and writes at most one move per side. A king
// on e1 with an all-but-the-king-empty board, clear castling paths, nothing pinned and no
// attacked square on rank 1 leaves every legality test passing, so the arbitrary rights
// drive Eva through every write/skip combination; the bound holds for all. The threat set
// is non-empty so threats_by reads it instead of computing it outside this unit.
static void check_generate_castling(void) {
    Position pos;
    StateInfo st;
    pos.st = &st;
    st.castling_rights = (CastlingRights) Frama_C_interval(NO_CASTLING, ANY_CASTLING);
    st.blockers[WHITE] = 0;
    st.threats[ALL_PIECES] = square_bb(SQ_H8);
    pos.by_type[KING] = square_bb(SQ_E1);
    pos.by_color[WHITE] = square_bb(SQ_E1);
    pos.by_type[ALL_PIECES] = square_bb(SQ_E1);
    for (int i = 0; i < 16; i++) {
        pos.castling_rook_square[i] = (Square) Frama_C_interval(0, 63);
        pos.castling_path[i] = 0;
    }
    ExtMove buf[2];
    const ExtMove *end = generate_castling(&pos, buf, WHITE);
    //@ assert castling_within: 0 <= end - buf <= 2;
}

// Point pos at a StateInfo and place WHITE/BLACK/pawn men and a white king on h1, out of
// every pawn's way, for the pawn-move positions. Nothing is pinned.
static void setup_pawns(Position *pos, StateInfo *st, Bitboard white, Bitboard black,
                        Bitboard white_pawns) {
    pos->st = st;
    st->ep_square = SQ_NONE;
    st->blockers[WHITE] = 0;
    pos->by_type[PAWN] = white_pawns;
    pos->by_type[KING] = square_bb(SQ_H1);
    pos->by_color[WHITE] = white | square_bb(SQ_H1);
    pos->by_color[BLACK] = black;
    pos->by_type[ALL_PIECES] = white | black | square_bb(SQ_H1);
}

// generate_pawn_moves over concrete pawn placements chosen to fire each branch. Rank masks:
// rank 2 is 0x...FF00, rank 5 is 0x...FF00000000, rank 7 is 0x00FF<<48.
static void check_generate_pawn_moves(void) {
    Position pos;
    StateInfo st;

    // Push-promotions: eight pawns on rank 7, board empty ahead (8 * 4 promotions).
    setup_pawns(&pos, &st, 0x00FF000000000000ULL, 0, 0x00FF000000000000ULL);
    {
        ExtMove buf[64];
        const ExtMove *end = generate_pawn_moves(&pos, buf, WHITE, ~0ULL, GEN_EVASIONS);
        //@ assert pawn_promo_within: 0 <= end - buf <= 64;
    }

    // Capture-promotions: eight pawns on rank 7, enemies across rank 8.
    setup_pawns(&pos, &st, 0x00FF000000000000ULL, 0xFF00000000000000ULL,
                0x00FF000000000000ULL);
    {
        ExtMove buf[64];
        const ExtMove *end = generate_pawn_moves(&pos, buf, WHITE, ~0ULL, GEN_EVASIONS);
        //@ assert pawn_capture_promo_within: 0 <= end - buf <= 64;
    }

    // Pushes and double pushes: eight pawns on rank 2, board empty ahead (8 + 8).
    setup_pawns(&pos, &st, 0x000000000000FF00ULL, 0, 0x000000000000FF00ULL);
    {
        ExtMove buf[32];
        const ExtMove *end = generate_pawn_moves(&pos, buf, WHITE, ~0ULL, GEN_EVASIONS);
        //@ assert pawn_push_within: 0 <= end - buf <= 32;
    }

    // Plain captures: eight pawns on rank 5, enemies across rank 6.
    setup_pawns(&pos, &st, 0x000000FF00000000ULL, 0x0000FF0000000000ULL,
                0x000000FF00000000ULL);
    {
        ExtMove buf[32];
        const ExtMove *end = generate_pawn_moves(&pos, buf, WHITE, ~0ULL, GEN_CAPTURES);
        //@ assert pawn_cap_within: 0 <= end - buf <= 32;
    }

    // En passant: pawns on rank 5 and an ep square on b6, which the a5 and c5 pawns both
    // reach along a capture diagonal.
    setup_pawns(&pos, &st, 0x000000FF00000000ULL, 0, 0x000000FF00000000ULL);
    st.ep_square = 41;  // b6
    {
        ExtMove buf[8];
        const ExtMove *end = generate_pawn_moves(&pos, buf, WHITE, ~0ULL, GEN_CAPTURES);
        //@ assert pawn_ep_within: 0 <= end - buf <= 8;
    }
}

// generate_piece_moves writes one move per (attack & target) bit per piece. Its write loop
// is the same for every piece type, so a leaper -- whose attacks_bb reads PseudoAttacks
// rather than the magic tables -- exercises it. Four knights, each given a concrete
// max-degree (8-target) attack set by hand, drive it to 32 writes with no overrun. A white
// king on h1 gives the generator its king square; nothing is pinned.
static void check_generate_piece_moves(void) {
    Position pos;
    StateInfo st;
    pos.st = &st;
    st.blockers[WHITE] = 0;
    const Bitboard knights =
      square_bb(18) | square_bb(21) | square_bb(42) | square_bb(45);
    pos.by_type[KNIGHT] = knights;
    pos.by_type[KING] = square_bb(SQ_H1);
    pos.by_color[WHITE] = knights | square_bb(SQ_H1);
    pos.by_type[ALL_PIECES] = knights | square_bb(SQ_H1);
    const Bitboard att = 0x00000000000000FFULL;  // a concrete 8-target stand-in
    PseudoAttacks[KNIGHT][18] = att;
    PseudoAttacks[KNIGHT][21] = att;
    PseudoAttacks[KNIGHT][42] = att;
    PseudoAttacks[KNIGHT][45] = att;
    ExtMove buf[64];
    const ExtMove *end = generate_piece_moves(&pos, buf, WHITE, KNIGHT, ~0ULL);
    //@ assert piece_within: 0 <= end - buf <= 64;
}

int eva_main(void) {
    check_make_promotions();
    check_generate_castling();
    check_generate_pawn_moves();
    check_generate_piece_moves();
    return 0;
}
