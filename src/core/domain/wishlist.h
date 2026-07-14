#pragma once

#include <set>
#include <string>

#include "core/domain/types.h"

namespace pokedex {

// COLLECTION (source of truth) — where to buy a Pokémon's card. Deliberately
// minimal: one Wishlist per Pokémon holding a set of free strings, each a
// seller's name or a marketplace link ("http…"). No prices, no card references
// — it exists only to help build a shopping list.
struct Wishlist {
    PokemonDexNum pokemonDexNum;
    std::set<std::string> sources;
    Timestamp insertedAt;  // UTC, set by app
    Timestamp updatedAt;   // UTC, set by app
};

}  // namespace pokedex
