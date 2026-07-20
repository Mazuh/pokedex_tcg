#pragma once

#include <unordered_map>
#include <vector>

#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/types.h"

namespace pokedex {

// GUI — bucket owned, species-tied copies by dex number: the single definition of
// "which copies drive the detail panel's copy mode". Both the binder guide
// (BinderView, over one binder's copies) and the Pokémon browser (PokemonListView,
// over every binder's copies) feed their copy list through here so the two views
// can never drift on the predicate — only Owned copies that depict a species
// (pokemonDexNum set) qualify, mirroring the "Completed" status and the Owned
// column's count. Species-free copies (Trainer/Energy) never appear in a
// species-oriented projection.
inline std::unordered_map<PokemonDexNum, std::vector<CardCopy>> bucketOwnedCopiesByDex(
    const std::vector<CardCopy>& copies) {
    std::unordered_map<PokemonDexNum, std::vector<CardCopy>> byDex;
    for (const CardCopy& copy : copies) {
        if (copy.pokemonDexNum && copy.ownership == CardOwnership::Owned) {
            byDex[*copy.pokemonDexNum].push_back(copy);
        }
    }
    return byDex;
}

}  // namespace pokedex
