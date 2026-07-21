#include "core/app/pokemon_browse_service.h"

#include <span>
#include <unordered_map>

#include "core/domain/pokemon_catalog.h"
#include "core/domain/types.h"
#include "core/storage/card_copy_repository.h"

namespace pokedex {

std::vector<PokemonBrowseEntry> PokemonBrowseService::listAll() {
    return listAll(copies_.ownedCountsByDexNum());
}

std::vector<PokemonBrowseEntry> PokemonBrowseService::listAll(
    const std::unordered_map<PokemonDexNum, int>& ownedCounts) const {
    const std::span<const Pokemon> catalog = pokemonCatalog();

    std::vector<PokemonBrowseEntry> entries;
    entries.reserve(catalog.size());
    for (const Pokemon& pokemon : catalog) {
        // A species absent from the count map owns nothing; default to zero.
        const auto it = ownedCounts.find(pokemon.dexNumber);
        const int owned = it != ownedCounts.end() ? it->second : 0;
        entries.push_back({pokemon, owned});
    }
    return entries;
}

}  // namespace pokedex
