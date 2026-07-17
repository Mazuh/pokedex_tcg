#pragma once

#include <string>

#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

// CATALOG — a Pokémon species (dex entry). Authoritative-but-fixed reference
// data: the full table is seeded as compile-time constants, so a Pokémon has no
// artificial id and no repository.
//
// A Pokémon is one of the fictional creatures that humans (Trainers) catch,
// train, and battle; each species has a name and originates from a Region.
//
// Identity is dexNumber (unique); name is likewise unique. The collection side
// never stores a Pokemon — it refers to one by PokemonDexNum.
struct Pokemon {
    PokemonDexNum dexNumber;
    std::string name;
    Region region;
};

}  // namespace pokedex
