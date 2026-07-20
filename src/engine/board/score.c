#include "score.h"

// Answer whether VALUE is decisive against the caller's win threshold. Kept
// private and threshold-parameterised; the header's value_is_decisive is the
// types.h-anchored public form.
static bool is_decisive_at(int32_t value, int32_t value_tb_win_in_max_ply) {
    return value >= value_tb_win_in_max_ply || value <= -value_tb_win_in_max_ply;
}

// The classifier always returns one of the three ScoreKind values, and -- the point of
// the contract -- computes `abs_value = -value` without the INT32_MIN overflow that a
// bare negation risks, given a bounded score and bounded thresholds (the callers pass
// the VALUE_* constants and a search score, all within a few tens of thousands). WP
// proves the kind is valid and, via -wp-rte, that neither the negation nor the plies
// subtractions overflow. (No `assigns \nothing`: the function takes only scalars by
// value, so it plainly writes no memory, and WP cannot discharge that clause through the
// by-value struct return.)
/*@ requires -100000 <= value <= 100000;
    requires 0 <= value_tb_win_in_max_ply <= 100000;
    requires 0 <= value_tb <= 100000;
    requires 0 <= value_mate <= 100000;
    ensures \result.kind == SCORE_NON_DECISIVE
         || \result.kind == SCORE_MATE
         || \result.kind == SCORE_TABLEBASE;
*/
ScoreClass score_classify(int32_t value,
                          int32_t value_tb_win_in_max_ply,
                          int32_t value_tb,
                          int32_t value_mate) {
    if (!is_decisive_at(value, value_tb_win_in_max_ply))
        return (ScoreClass) { .kind = SCORE_NON_DECISIVE, .plies = 0, .win = false };

    const int32_t abs_value = value < 0 ? -value : value;

    if (abs_value <= value_tb) {
        const int32_t distance = value_tb - abs_value;
        return (ScoreClass) { .kind = SCORE_TABLEBASE,
                              .plies = value > 0 ? distance : -distance,
                              .win = value > 0 };
    }

    const int32_t distance = value_mate - abs_value;
    return (ScoreClass) { .kind = SCORE_MATE,
                          .plies = value > 0 ? distance : -distance,
                          .win = value > 0 };
}
