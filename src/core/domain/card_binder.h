#pragma once

#include <string>
#include <vector>

#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

// COLLECTION (source of truth) — a physical album that stores and displays
// cards in plastic pockets and pages (usually ungraded, unless a slab binder).
// It owns nothing directly; it is a lens. Copies point back at it via binderId,
// and its per-Pokémon "guide" is computed lazily, never stored.
//
// pokemonRegions is the set of regions the binder is scoped to — a binder may
// span more than one (e.g. a "Kanto + Johto" album), so the guide lists every
// species to capture across all of them (including ones the user owns no copy of
// yet). An empty vector means the binder is region-less: its guide shows only the
// species it already has cards filed for. Held as a vector kept in canonical
// (enum) order with no duplicates — the app/storage layers enforce that shape.
struct CardBinder {
    CardBinderId id;
    std::string name;
    std::vector<Region> pokemonRegions;
    Timestamp insertedAt;  // UTC, set by app
    Timestamp updatedAt;   // UTC, set by app
};

}  // namespace pokedex
