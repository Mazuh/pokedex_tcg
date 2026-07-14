#include <gtest/gtest.h>

#include "core/domain/pokemon.h"

namespace {

using pokedex::Pokemon;
using pokedex::Region;

TEST(PokemonTest, HoldsDexNumberNameAndRegion) {
    Pokemon bulbasaur{.dexNumber = 1, .name = "Bulbasaur", .region = Region::Kanto};
    EXPECT_EQ(bulbasaur.dexNumber, 1);
    EXPECT_EQ(bulbasaur.name, "Bulbasaur");
    EXPECT_EQ(bulbasaur.region, Region::Kanto);
}

}  // namespace
