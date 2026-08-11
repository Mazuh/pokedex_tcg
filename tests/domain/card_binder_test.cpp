#include <gtest/gtest.h>

#include "core/domain/card_binder.h"

namespace {

using pokedex::CardBinder;
using pokedex::CardBinderBlank;
using pokedex::CardBinderPocketGrid;
using pokedex::Region;

TEST(CardBinderTest, RegionlessByDefault) {
    CardBinder binder{};
    EXPECT_TRUE(binder.pokemonRegions.empty());
}

// The physical layout is optional throughout: a binder recorded before it existed,
// or one whose owner never measured the album, simply says nothing.
TEST(CardBinderTest, PhysicalLayoutIsUnsetByDefault) {
    CardBinder binder{};
    EXPECT_FALSE(binder.capacity.has_value());
    EXPECT_FALSE(binder.pocketGrid.has_value());
    EXPECT_TRUE(binder.pocketBlanks.empty());
}

TEST(CardBinderTest, RemembersCapacityAndPocketGrid) {
    CardBinder binder{
        .id = "binder-1",
        .name = "Kanto Journey",
        .capacity = 360,
        .pocketGrid = CardBinderPocketGrid{.rows = 3, .columns = 3},
    };
    ASSERT_TRUE(binder.capacity.has_value());
    EXPECT_EQ(*binder.capacity, 360);
    ASSERT_TRUE(binder.pocketGrid.has_value());
    EXPECT_EQ(binder.pocketGrid->rows, 3);
    EXPECT_EQ(binder.pocketGrid->columns, 3);
}

// A page holds rows x columns cards — the figure the guide pages by.
TEST(CardBinderTest, PocketsPerPageMultipliesTheGrid) {
    EXPECT_EQ(pocketsPerPage(CardBinderPocketGrid{.rows = 3, .columns = 3}), 9);
    EXPECT_EQ(pocketsPerPage(CardBinderPocketGrid{.rows = 4, .columns = 3}), 12);
    EXPECT_EQ(pocketsPerPage(CardBinderPocketGrid{.rows = 1, .columns = 2}), 2);
}

// Exactly one anchor is set: a species (the durable choice) or one exact filed
// card (for a species-free row, which has no dex number to name it).
TEST(CardBinderTest, BlankNamesEitherASpeciesOrAnExactCard) {
    const CardBinderBlank bySpecies{.beforeDexNum = 650, .blanks = 2};
    EXPECT_EQ(bySpecies.beforeDexNum, 650);
    EXPECT_FALSE(bySpecies.beforeCopyId.has_value());
    EXPECT_EQ(bySpecies.blanks, 2);

    const CardBinderBlank byCopy{.beforeCopyId = "copy-7", .blanks = 1};
    EXPECT_FALSE(byCopy.beforeDexNum.has_value());
    EXPECT_EQ(byCopy.beforeCopyId, "copy-7");
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
