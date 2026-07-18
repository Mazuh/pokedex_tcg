#include "core/app/binder_guide_service.h"

#include <map>
#include <set>
#include <span>

#include "core/domain/card_copy.h"
#include "core/domain/collection_status.h"
#include "core/domain/pokemon.h"
#include "core/domain/pokemon_catalog.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/wishlist_repository.h"

namespace pokedex {

namespace {

// Which ownership states a species has among the copies filed in this binder.
struct FiledStates {
    bool incoming = false;
    bool owned = false;
    bool removed = false;
};

// Bucket the binder's copies by species in a single pass, so status resolution
// is a map lookup per dex number rather than a rescan of every copy.
std::map<PokemonDexNum, FiledStates> filedStatesByDex(
    const std::vector<CardCopy>& copiesInBinder) {
    std::map<PokemonDexNum, FiledStates> byDex;
    for (const CardCopy& copy : copiesInBinder) {
        if (!copy.pokemonDexNum) {
            continue;  // a species-free card (Trainer/Energy) has no guide row
        }
        FiledStates& states = byDex[*copy.pokemonDexNum];
        switch (copy.ownership) {
            case CardOwnership::Incoming: states.incoming = true; break;
            case CardOwnership::Owned:    states.owned = true;    break;
            case CardOwnership::Removed:  states.removed = true;  break;
        }
    }
    return byDex;
}

// Resolve one Pokémon's standing within the binder, in the first-match-wins
// precedence documented on CollectionStatus. `filedHere` is the species' filed
// states in this binder (default-empty when it has none); `ownedElsewhere` /
// `wished` are the precomputed sets.
CollectionStatus resolveStatus(PokemonDexNum dexNum, const FiledStates& filedHere,
                               const std::set<PokemonDexNum>& ownedElsewhere,
                               const std::set<PokemonDexNum>& wished) {
    if (filedHere.incoming) return CollectionStatus::Incoming;
    if (filedHere.owned) return CollectionStatus::Completed;
    if (wished.contains(dexNum)) return CollectionStatus::Wished;
    if (ownedElsewhere.contains(dexNum)) return CollectionStatus::Elsewhere;
    if (filedHere.removed) return CollectionStatus::Removed;
    return CollectionStatus::Incomplete;
}

}  // namespace

std::vector<CardBinderEntry> BinderGuideService::buildEntries(const CardBinder& binder) {
    const std::vector<CardCopy> copiesInBinder = copies_.listByBinder(binder.id);
    const std::vector<PokemonDexNum> elsewhereList = copies_.ownedElsewhere(binder.id);
    const std::vector<PokemonDexNum> wishedList = wishlist_.wishedDexNums();
    const std::set<PokemonDexNum> ownedElsewhere(elsewhereList.begin(), elsewhereList.end());
    const std::set<PokemonDexNum> wished(wishedList.begin(), wishedList.end());
    const std::map<PokemonDexNum, FiledStates> filedHere = filedStatesByDex(copiesInBinder);

    const std::span<const Pokemon> catalog = pokemonCatalog();

    // The ordered row set: the binder's region species (if any) unioned with any
    // species that has a copy filed here. std::set keeps it dex-ordered and
    // deduplicated (a filed in-region species appears once).
    std::set<PokemonDexNum> dexNums;
    if (binder.pokemonRegion) {
        for (const Pokemon& pokemon : catalog) {
            if (pokemon.region == *binder.pokemonRegion) {
                dexNums.insert(pokemon.dexNumber);
            }
        }
    }
    for (const CardCopy& copy : copiesInBinder) {
        if (copy.pokemonDexNum) {
            dexNums.insert(*copy.pokemonDexNum);
        }
    }

    std::vector<CardBinderEntry> entries;
    entries.reserve(dexNums.size());
    for (const PokemonDexNum dexNum : dexNums) {
        // The catalog is contiguous over 1..N by dex number, so a valid number
        // indexes straight in. A number outside that range (a hand-edited row)
        // has no species to show, so it is skipped rather than fabricated.
        if (dexNum < 1 || static_cast<std::size_t>(dexNum) > catalog.size()) {
            continue;
        }
        const Pokemon& pokemon = catalog[static_cast<std::size_t>(dexNum) - 1];
        const auto states = filedHere.find(dexNum);
        const FiledStates& filed =
            states != filedHere.end() ? states->second : FiledStates{};
        entries.push_back(
            {pokemon, resolveStatus(dexNum, filed, ownedElsewhere, wished)});
    }
    return entries;
}

}  // namespace pokedex
