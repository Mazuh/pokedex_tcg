#include "core/app/binder_guide_service.h"

#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <vector>

#include "core/domain/card_copy.h"
#include "core/domain/collection_status.h"
#include "core/domain/pokemon.h"
#include "core/domain/pokemon_catalog.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/wishlist_repository.h"

namespace pokedex {

namespace {

// The catalog species a dex number names, or nullptr when it names none. The catalog is
// contiguous over 1..N by dex number, so a valid number indexes straight in.
//
// This is deliberately ONE helper rather than the range check spelled out at each use.
// The guide's contract is that nothing filed here is invisible, and that rests on the
// partition below and the emit loop agreeing EXACTLY on which dex numbers resolve: if the
// partition kept a copy that the emit loop then skipped, that card would silently vanish
// from the guide — the very bug this row model exists to fix, and one no test would catch
// unless it probed the boundary.
const Pokemon* speciesAt(PokemonDexNum dexNum, std::span<const Pokemon> catalog) {
    if (dexNum < 1 || static_cast<std::size_t>(dexNum) > catalog.size()) {
        return nullptr;
    }
    return &catalog[static_cast<std::size_t>(dexNum) - 1];
}

// One filed copy's own standing. A copy row speaks for exactly one physical card,
// so its status is a direct reading of that card's ownership — no precedence runs
// between the copies of a species, since each gets its own row.
CollectionStatus statusOfCopy(CardOwnership ownership) {
    switch (ownership) {
        case CardOwnership::Incoming: return CollectionStatus::Incoming;
        case CardOwnership::Owned:    return CollectionStatus::Completed;
        case CardOwnership::Removed:  return CollectionStatus::Removed;
    }
    return CollectionStatus::Incomplete;
}

// A listed species' standing when it has NO copy filed in this binder, in the
// first-match-wins precedence CollectionStatus documents. Only three of the six
// cases can arise: Incoming / Completed / Removed each require a copy filed here,
// and such a species gets copy rows instead of a placeholder — so by construction
// they are unreachable from here.
CollectionStatus placeholderStatus(PokemonDexNum dexNum,
                                   const std::set<PokemonDexNum>& ownedElsewhere,
                                   const std::set<PokemonDexNum>& wished) {
    if (wished.contains(dexNum)) return CollectionStatus::Wished;
    if (ownedElsewhere.contains(dexNum)) return CollectionStatus::Elsewhere;
    return CollectionStatus::Incomplete;
}

}  // namespace

std::vector<CardBinderEntry> BinderGuideService::buildEntries(const CardBinder& binder) {
    const std::vector<CardCopy> copiesInBinder = copies_.listByBinder(binder.id);
    const std::vector<PokemonDexNum> elsewhereList = copies_.ownedElsewhere(binder.id);
    const std::vector<PokemonDexNum> wishedList = wishlist_.wishedDexNums();
    const std::set<PokemonDexNum> ownedElsewhere(elsewhereList.begin(), elsewhereList.end());
    const std::set<PokemonDexNum> wished(wishedList.begin(), wishedList.end());

    const std::span<const Pokemon> catalog = pokemonCatalog();

    // Partition the filed copies once. The repository returns them in filed order
    // (inserted_at, rowid), so push_back preserves that within a species, and the
    // std::map keeps the species themselves dex-ordered. A copy whose dex number
    // doesn't resolve in the catalog joins the species-free tail rather than being
    // dropped: this guide's contract is that nothing filed here is invisible.
    // The pointers reference copiesInBinder, which outlives this whole function.
    std::map<PokemonDexNum, std::vector<const CardCopy*>> copiesByDex;
    std::vector<const CardCopy*> tail;
    for (const CardCopy& copy : copiesInBinder) {
        if (copy.pokemonDexNum && speciesAt(*copy.pokemonDexNum, catalog) != nullptr) {
            copiesByDex[*copy.pokemonDexNum].push_back(&copy);
        } else {
            tail.push_back(&copy);
        }
    }

    // The ordered species set: every species in any of the binder's regions,
    // unioned with any species that has a copy filed here. std::set keeps it
    // dex-ordered and deduplicated (a filed in-region species, or a species shared
    // across two scoped regions, appears once).
    std::set<PokemonDexNum> dexNums;
    if (!binder.pokemonRegions.empty()) {
        const std::set<Region> scoped(binder.pokemonRegions.begin(),
                                      binder.pokemonRegions.end());
        for (const Pokemon& pokemon : catalog) {
            if (scoped.contains(pokemon.region)) {
                dexNums.insert(pokemon.dexNumber);
            }
        }
    }
    for (const auto& filed : copiesByDex) {
        dexNums.insert(filed.first);
    }

    // The row count is no longer bounded by the catalog — a binder holding many
    // duplicates has more rows than it lists species.
    std::vector<CardBinderEntry> entries;
    entries.reserve(dexNums.size() + copiesInBinder.size());
    for (const PokemonDexNum dexNum : dexNums) {
        // The partition above routed every unresolvable filed copy to the tail through
        // this same helper, so a bucketed species always resolves here — this guard now
        // only covers a hypothetical bad region entry, whose number has no species to
        // show and is skipped rather than fabricated.
        const Pokemon* species = speciesAt(dexNum, catalog);
        if (species == nullptr) {
            continue;
        }
        const Pokemon& pokemon = *species;
        const auto bucket = copiesByDex.find(dexNum);
        if (bucket == copiesByDex.end()) {
            entries.push_back({pokemon, std::nullopt,
                               placeholderStatus(dexNum, ownedElsewhere, wished)});
            continue;
        }
        // One row per filed copy, in filed order, and NO placeholder — the copies
        // themselves are what this species has to say about this binder.
        for (const CardCopy* copy : bucket->second) {
            entries.push_back({pokemon, copy->id, statusOfCopy(copy->ownership)});
        }
    }

    // Species-free cards last: they carry no dex number, so there is nowhere among
    // the species rows to sort them.
    for (const CardCopy* copy : tail) {
        entries.push_back({std::nullopt, copy->id, statusOfCopy(copy->ownership)});
    }
    return entries;
}

}  // namespace pokedex
