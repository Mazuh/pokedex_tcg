#include <gtest/gtest.h>

#include "core/domain/card_binder.h"

namespace {

using pokedex::CardBinder;
using pokedex::Region;

TEST(CardBinderTest, RegionlessByDefault) {
    CardBinder binder{};
    EXPECT_FALSE(binder.pokemonRegion.has_value());
}

TEST(CardBinderTest, RemembersInitializingRegion) {
    CardBinder binder{
        .id = "binder-1",
        .name = "Kanto Journey",
        .pokemonRegion = Region::Kanto,
    };
    EXPECT_EQ(binder.id, "binder-1");
    EXPECT_EQ(binder.name, "Kanto Journey");
    ASSERT_TRUE(binder.pokemonRegion.has_value());
    EXPECT_EQ(*binder.pokemonRegion, Region::Kanto);
}

}  // namespace
