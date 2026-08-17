#include "core/app/pokemon_browse_service.h"

#include <cstddef>
#include <span>
#include <unordered_map>

#include "core/domain/pokemon_catalog.h"
#include "core/domain/region.h"
#include "core/domain/types.h"
#include "core/storage/card_copy_repository.h"

namespace pokedex {

namespace {

// regionProgress indexes its output array by the enumerator's own value, which is
// sound only while kRegions is declared in enumerator order. Assert that here so a
// future reorder is a compile error rather than a silent mis-tally — the same trick
// kRegions itself plays with its size.
constexpr bool kRegionsAreInEnumeratorOrder = [] {
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        if (kRegions[i] != static_cast<Region>(i)) {
            return false;
        }
    }
    return true;
}();
static_assert(kRegionsAreInEnumeratorOrder,
              "kRegions must list the enumerators in declaration order");

}  // namespace

std::array<RegionProgress, kRegions.size()> regionProgress(
    std::span<const PokemonBrowseEntry> entries) {
    // Seed every region first, so one with no species (or no entries at all) still
    // reports a row rather than vanishing from the breakdown.
    std::array<RegionProgress, kRegions.size()> progress{};
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        progress[i] = RegionProgress{kRegions[i], 0, 0, 0};
    }
    for (const PokemonBrowseEntry& entry : entries) {
        RegionProgress& row = progress[static_cast<std::size_t>(entry.pokemon.region)];
        ++row.totalSpecies;
        row.cards += entry.ownedCount;
        if (entry.ownedCount > 0) {
            ++row.capturedSpecies;
        }
    }
    return progress;
}

CollectionProgress totalProgress(std::span<const RegionProgress> regions) {
    CollectionProgress total{0, 0, 0};
    for (const RegionProgress& row : regions) {
        total.capturedSpecies += row.capturedSpecies;
        total.totalSpecies += row.totalSpecies;
        total.cards += row.cards;
    }
    return total;
}

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
