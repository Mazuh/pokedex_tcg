#pragma once

#include <optional>
#include <string>

#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

// COLLECTION (source of truth) — a physical album. It owns nothing directly; it
// is a lens. Copies point back at it via binderId, and its per-Pokémon "guide"
// is computed lazily, never stored.
//
// pokemonRegion, when set, remembers the region the binder was initialized from
// so the guide can list every species to capture — including ones the user owns
// no copy of yet.
struct CardBinder {
    CardBinderId id;
    std::string name;
    std::optional<Region> pokemonRegion;
    Timestamp insertedAt;  // UTC, set by app
    Timestamp updatedAt;   // UTC, set by app
};

}  // namespace pokedex
