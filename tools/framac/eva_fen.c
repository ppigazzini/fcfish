// Frama-C Eva harness for the FEN parser's input safety.
//
// pos_set_reason parses an untrusted FEN string and places pieces with
// `put_piece(pos, ..., make_square(f, r))`, writing pos->board[make_square(f, r)]. A
// malformed FEN must never drive that index out of [0, 63]: the parser guards it with
// `f >= 8` and `r < 0` checks (and `> 32 pieces`, invalid tokens, ...) that reject the
// input before the write. This harness feeds adversarial and boundary FENs and proves --
// through 0 alarms -- that no board write ever lands out of bounds.
//
// Every FEN here fails BEFORE set_check_info, the step that reads the magic attack tables
// attacks_init fills (which Eva cannot run). A structurally-valid two-king board would
// reach that step, so the inputs are chosen to fail the parse, a guard, or a post-parse
// legality check first -- which is exactly where the board-write safety is decided.
//
// This is bounded-model-checking coverage: the parser's write safety on these
// representative inputs, not a proof over every possible string -- a symbolic FEN explodes
// Eva's state space (a dozen free bytes through the parse branches do not terminate in
// gate time).
#include "engine/board/fen.h"
#include "engine/board/position.h"

#include "__fc_builtin.h"

int eva_main(void) {
    Position pos;
    StateInfo st;

    // Nine pieces in a rank: the `f >= 8` guard must stop the ninth board write.
    (void) pos_set_reason(&pos, "ppppppppp/8/8/8/8/8/8/8 w - - 0 1", false, &st, 0);
    // Nine ranks: the rank cursor must not run below 0.
    (void) pos_set_reason(&pos, "8/8/8/8/8/8/8/8/8 w - - 0 1", false, &st, 0);
    // Empty board: parses to completion (0 writes) then fails the king count.
    (void) pos_set_reason(&pos, "8/8/8/8/8/8/8/8 w - - 0 1", false, &st, 0);
    // Sixty-four pieces: the `> 32 pieces` guard fires mid-board.
    (void) pos_set_reason(
      &pos,
      "nnnnnnnn/nnnnnnnn/nnnnnnnn/nnnnnnnn/nnnnnnnn/nnnnnnnn/nnnnnnnn/nnnnnnnn w - - 0 1",
      false, &st, 0);
    // A full 31-piece board missing a king: writes 31 squares, then fails the king count
    // before any attack table is read.
    (void) pos_set_reason(&pos, "rnbq1bnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w - - 0 1", false,
                          &st, 0);
    // Degenerate strings: empty, and only rank separators.
    (void) pos_set_reason(&pos, "", false, &st, 0);
    (void) pos_set_reason(&pos, "///////", false, &st, 0);
    return 0;
}
