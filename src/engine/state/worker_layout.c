#include "worker_layout.h"

#include <string.h>

void search_manager_clear(SearchManager *sm) {
    timeman_clear(&sm->tm);
    atomic_bool_store(&sm->ponder, false);

    // -1.0 is upstream's "not yet fixed" for the time adjust, and 0.85 its
    // time-reduction seed. Neither is zero, and zero for either makes every move take
    // the first-move-of-a-game path.
    sm->original_time_adjust = -1.0;
    sm->previous_time_reduction = 0.85;

    // VALUE_INFINITE is upstream's "no previous score" for both.
    sm->best_previous_score = VALUE_INFINITE;
    sm->best_previous_average_score = VALUE_INFINITE;

    memset(sm->iter_value, 0, sizeof sm->iter_value);
    sm->calls_cnt = 0;
    sm->stop_on_ponderhit = false;
}
