#pragma once

#include "core/domain/collection_status.h"
#include "core/domain/pokemon.h"

namespace pokedex {

// INFERRED (projection) — one row of a binder's guide: a Pokémon paired with
// its CollectionStatus in that binder. Built on demand by an app-layer service
// (buildBinderEntries) from the source-of-truth entities; it has no id, no
// repository, and is never mutated — only recomputed.
//
// It carries just the verdict. For the underlying copies or wishlist sources,
// the app queries those entities directly.
struct CardBinderEntry {
    Pokemon pokemon;
    CollectionStatus status;
};

}  // namespace pokedex
