#include <gtest/gtest.h>

#include "core/domain/domain.h"

namespace {

using pokedex::CardBinderEntry;
using pokedex::CollectionStatus;
using pokedex::Pokemon;
using pokedex::Region;

// A binder guide row is one of exactly three shapes (see CardBinderEntry): a
// species placeholder, a copy of a species, or a species-free card. Pin all three
// so the projection can't quietly lose one.
TEST(DomainModelTest, BinderEntryPlaceholderRowPairsPokemonWithStatus) {
    CardBinderEntry entry{
        .pokemon = Pokemon{.dexNumber = 3, .name = "Venusaur", .region = Region::Kanto},
        .status = CollectionStatus::Incomplete,
    };
    ASSERT_TRUE(entry.pokemon.has_value());
    EXPECT_EQ(entry.pokemon->dexNumber, 3);
    EXPECT_EQ(entry.pokemon->name, "Venusaur");
    EXPECT_FALSE(entry.cardCopyId.has_value());  // no card behind a placeholder
    EXPECT_EQ(entry.status, CollectionStatus::Incomplete);
}

TEST(DomainModelTest, BinderEntryCopyRowNamesBothTheSpeciesAndTheCopy) {
    CardBinderEntry entry{
        .pokemon = Pokemon{.dexNumber = 3, .name = "Venusaur", .region = Region::Kanto},
        .cardCopyId = "copy-1",
        .status = CollectionStatus::Completed,
    };
    ASSERT_TRUE(entry.pokemon.has_value());
    EXPECT_EQ(entry.pokemon->dexNumber, 3);
    EXPECT_EQ(entry.cardCopyId, "copy-1");
    EXPECT_EQ(entry.status, CollectionStatus::Completed);
}

TEST(DomainModelTest, BinderEntrySpeciesFreeRowCarriesOnlyTheCopy) {
    CardBinderEntry entry{
        .cardCopyId = "copy-2",
        .status = CollectionStatus::Completed,
    };
    EXPECT_FALSE(entry.pokemon.has_value());  // a Trainer/Energy card depicts none
    EXPECT_EQ(entry.cardCopyId, "copy-2");
    EXPECT_EQ(entry.status, CollectionStatus::Completed);
}

// The enum is declared in first-match-wins precedence order; the app relies on
// that ordering when resolving a Pokémon's status within a binder.
TEST(DomainModelTest, StatusPrecedenceRunsIncomingFirstToIncompleteLast) {
    EXPECT_LT(static_cast<int>(CollectionStatus::Incoming),
              static_cast<int>(CollectionStatus::Completed));
    EXPECT_LT(static_cast<int>(CollectionStatus::Completed),
              static_cast<int>(CollectionStatus::Wished));
    EXPECT_LT(static_cast<int>(CollectionStatus::Wished),
              static_cast<int>(CollectionStatus::Elsewhere));
    EXPECT_LT(static_cast<int>(CollectionStatus::Elsewhere),
              static_cast<int>(CollectionStatus::Removed));
    EXPECT_LT(static_cast<int>(CollectionStatus::Removed),
              static_cast<int>(CollectionStatus::Incomplete));
}

}  // namespace
