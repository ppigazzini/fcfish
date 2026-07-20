// Frama-C WP driver: a translation unit that pulls in the headers whose functions
// carry ACSL contracts, so `frama-c -wp` has their bodies to prove. WP is restricted
// to the contracted, non-bitwise helpers (see tools/framac/wp.sh -wp-fct); the bitwise
// codec/bitboard helpers are covered by the Eva gate instead, because WP's shift
// encoding is intractable for the available SMT provers.
#include "engine/board/types.h"
#include "engine/search/history.h"
