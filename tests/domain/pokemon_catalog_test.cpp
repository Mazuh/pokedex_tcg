#include <gtest/gtest.h>

#include <set>
#include <string>

#include "core/domain/pokemon_catalog.h"

namespace {

using pokedex::Pokemon;
using pokedex::pokemonCatalog;
using pokedex::Region;

TEST(PokemonCatalogTest, CoversNationalDexOneThroughOneThousandTwentyFive) {
    const auto catalog = pokemonCatalog();
    ASSERT_EQ(catalog.size(), 1025u);
    EXPECT_EQ(catalog.front().name, "Bulbasaur");
    EXPECT_EQ(catalog.back().name, "Pecharunt");
}

// The collection side keys on dexNumber, so the catalog must be a clean lookup
// table: sorted, gap-free over 1..1025, with unique numbers.
TEST(PokemonCatalogTest, DexNumbersAreContiguousAndUnique) {
    const auto catalog = pokemonCatalog();
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        EXPECT_EQ(catalog[i].dexNumber, static_cast<int>(i) + 1);
    }
}

// pokemon.h documents that a species' name is unique, too (autocomplete relies
// on it) — this is why Nidoran♀/♂ are disambiguated rather than left colliding.
TEST(PokemonCatalogTest, NamesAreUniqueAndNonEmpty) {
    const auto catalog = pokemonCatalog();
    std::set<std::string> names;
    for (const Pokemon& p : catalog) {
        EXPECT_FALSE(p.name.empty()) << "dex #" << p.dexNumber << " has no name";
        EXPECT_TRUE(names.insert(p.name).second) << "duplicate name: " << p.name;
    }
    EXPECT_EQ(names.size(), catalog.size());
}

// Region is derived from generation; spot-check the boundaries of a few
// generation ranges to pin that mapping.
TEST(PokemonCatalogTest, RegionMatchesGenerationRanges) {
    const auto catalog = pokemonCatalog();
    auto regionOf = [&](int dexNumber) { return catalog[dexNumber - 1].region; };
    EXPECT_EQ(regionOf(1), Region::Kanto);      // gen 1: 1..151
    EXPECT_EQ(regionOf(151), Region::Kanto);
    EXPECT_EQ(regionOf(152), Region::Johto);    // gen 2: 152..251
    EXPECT_EQ(regionOf(386), Region::Hoenn);    // gen 3 ends at 386
    EXPECT_EQ(regionOf(722), Region::Alola);    // gen 7: 722..809
    EXPECT_EQ(regionOf(906), Region::Paldea);   // gen 9: 906..1025
    EXPECT_EQ(regionOf(1025), Region::Paldea);
}

}  // namespace
