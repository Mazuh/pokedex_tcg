#include <gtest/gtest.h>

#include "core/domain/card_condition.h"

namespace {

using pokedex::CardCondition;

// CardCondition is declared best-to-worst; code that compares grades relies on
// that order, so lock it the way we lock CollectionStatus precedence.
TEST(CardConditionTest, IsOrderedBestToWorst) {
    EXPECT_LT(static_cast<int>(CardCondition::NearMint),
              static_cast<int>(CardCondition::LightlyPlayed));
    EXPECT_LT(static_cast<int>(CardCondition::LightlyPlayed),
              static_cast<int>(CardCondition::ModeratelyPlayed));
    EXPECT_LT(static_cast<int>(CardCondition::ModeratelyPlayed),
              static_cast<int>(CardCondition::HeavilyPlayed));
    EXPECT_LT(static_cast<int>(CardCondition::HeavilyPlayed),
              static_cast<int>(CardCondition::Damaged));
}

}  // namespace
