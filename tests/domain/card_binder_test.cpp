#include <gtest/gtest.h>

#include "core/domain/card_binder.h"

namespace {

using pokedex::CardBinder;
using pokedex::Region;

TEST(CardBinderTest, RegionlessByDefault) {
    CardBinder binder{};
    EXPECT_TRUE(binder.pokemonRegions.empty());
}

TEST(CardBinderTest, RemembersScopedRegions) {
    CardBinder binder{
        .id = "binder-1",
        .name = "Johto Adventure",
        .pokemonRegions = {Region::Kanto, Region::Johto},
    };
    EXPECT_EQ(binder.id, "binder-1");
    EXPECT_EQ(binder.name, "Johto Adventure");
    ASSERT_EQ(binder.pokemonRegions.size(), 2u);
    EXPECT_EQ(binder.pokemonRegions[0], Region::Kanto);
    EXPECT_EQ(binder.pokemonRegions[1], Region::Johto);
}

}  // namespace
