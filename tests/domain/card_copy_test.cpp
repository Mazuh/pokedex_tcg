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

}  // namespace
