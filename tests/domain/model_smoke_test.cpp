#include <gtest/gtest.h>

#include "core/domain/domain.h"

namespace {

using pokedex::CardBinderEntry;
using pokedex::CollectionStatus;
using pokedex::Pokemon;
using pokedex::Region;

TEST(DomainModelTest, BinderEntryPairsPokemonWithStatus) {
    CardBinderEntry entry{
        .pokemon = Pokemon{.dexNumber = 3, .name = "Venusaur", .region = Region::Kanto},
        .status = CollectionStatus::Completed,
    };
    EXPECT_EQ(entry.pokemon.dexNumber, 3);
    EXPECT_EQ(entry.pokemon.name, "Venusaur");
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
