#include <gtest/gtest.h>

#include "core/domain/domain.h"

namespace {

using pokedex::CardBinderEntry;
using pokedex::CollectionStatus;
using pokedex::holdsPocket;
using pokedex::Pokemon;
using pokedex::Region;

// A binder guide row is one of exactly four shapes (see CardBinderEntry): a
// species placeholder, a copy of a species, a species-free card, or a deliberate
// blank pocket. Pin all four so the projection can't quietly lose one.
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

// The fourth shape: a pocket the user deliberately left empty to push what follows
// onto a fresh page. It stands for neither a species nor a card, so there is
// nothing for it to report a CollectionStatus about.
TEST(DomainModelTest, BinderEntryBlankRowNamesNeitherSpeciesNorCopyNorStatus) {
    CardBinderEntry entry{};
    EXPECT_FALSE(entry.pokemon.has_value());
    EXPECT_FALSE(entry.cardCopyId.has_value());
    EXPECT_FALSE(entry.status.has_value());
}

// Which rows occupy a physical pocket decides where every page break falls, so the
// predicate is pinned across all four shapes rather than left to each reader. The
// interesting cases are the two that hold NO card and still take a sleeve — a
// placeholder (reserved for that species) and a blank (whose purpose IS to take one).
TEST(DomainModelTest, EveryRowHoldsAPocketExceptARemovedCopy) {
    const Pokemon venusaur{.dexNumber = 3, .name = "Venusaur", .region = Region::Kanto};

    EXPECT_TRUE(holdsPocket(CardBinderEntry{}));  // a blank pocket
    EXPECT_TRUE(holdsPocket(CardBinderEntry{.pokemon = venusaur,
                                            .status = CollectionStatus::Incomplete}));
    EXPECT_TRUE(holdsPocket(CardBinderEntry{
        .pokemon = venusaur, .cardCopyId = "copy-1", .status = CollectionStatus::Completed}));
    EXPECT_TRUE(holdsPocket(
        CardBinderEntry{.cardCopyId = "copy-2", .status = CollectionStatus::Incoming}));

    // The one exception: frozen history, listed and grayed but not in the sleeve.
    EXPECT_FALSE(holdsPocket(CardBinderEntry{
        .pokemon = venusaur, .cardCopyId = "copy-3", .status = CollectionStatus::Removed}));
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
