#include <gtest/gtest.h>

#include "core/domain/card_copy.h"

namespace {

using pokedex::CardCondition;
using pokedex::CardCopy;
using pokedex::CardOwnership;
using pokedex::CardReference;

TEST(CardCopyTest, UnfiledByDefault) {
    CardCopy copy{};
    EXPECT_FALSE(copy.binderId.has_value());
}

TEST(CardCopyTest, RecordsSpeciesPrintingStateAndCondition) {
    CardCopy copy{
        .id = "copy-1",
        .pokemonDexNum = 3,  // Venusaur
        .cardRef = CardReference{"MEW", "EN", "003/165"},
        .ownership = CardOwnership::Owned,
        .condition = CardCondition::NearMint,
        .binderId = "binder-1",
        .comments = "Bought at a fair in 2020.",
    };
    EXPECT_EQ(copy.pokemonDexNum, 3);
    EXPECT_EQ(copy.cardRef.expansionCode, "MEW");
    EXPECT_EQ(copy.ownership, CardOwnership::Owned);
    EXPECT_EQ(copy.condition, CardCondition::NearMint);
    ASSERT_TRUE(copy.binderId.has_value());
    EXPECT_EQ(*copy.binderId, "binder-1");
}

// A card that depicts no species (a Trainer/Energy card) records no dex number; its
// printed name on the reference is then the only human-readable label it has.
TEST(CardCopyTest, RecordsASpeciesFreeCard) {
    CardCopy copy{
        .id = "copy-2",
        .pokemonDexNum = std::nullopt,
        .cardRef = CardReference{.collectorNumber = "196/198", .name = "Boss's Orders"},
        .ownership = CardOwnership::Owned,
    };
    EXPECT_FALSE(copy.pokemonDexNum.has_value());
    EXPECT_EQ(copy.cardRef.name, "Boss's Orders");
}

}  // namespace
