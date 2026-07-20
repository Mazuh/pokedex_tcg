#include <gtest/gtest.h>

#include "core/domain/card_rarity.h"

namespace {

using pokedex::CardRarity;

// The rarity picker and its info popover rely on the modern scale coming first and
// the legacy rarities being grouped at the end; storage sort keys rank by this
// order too. Pin the declaration order so a reorder is caught.
TEST(CardRarityTest, ModernScaleThenLegacyGroupInOrder) {
    // Modern scale, in ascending declaration order.
    EXPECT_LT(static_cast<int>(CardRarity::Common), static_cast<int>(CardRarity::Uncommon));
    EXPECT_LT(static_cast<int>(CardRarity::Uncommon), static_cast<int>(CardRarity::Rare));
    EXPECT_LT(static_cast<int>(CardRarity::Rare), static_cast<int>(CardRarity::DoubleRare));
    EXPECT_LT(static_cast<int>(CardRarity::DoubleRare),
              static_cast<int>(CardRarity::IllustrationRare));
    EXPECT_LT(static_cast<int>(CardRarity::IllustrationRare),
              static_cast<int>(CardRarity::UltraRare));
    EXPECT_LT(static_cast<int>(CardRarity::UltraRare),
              static_cast<int>(CardRarity::SpecialIllustrationRare));
    EXPECT_LT(static_cast<int>(CardRarity::SpecialIllustrationRare),
              static_cast<int>(CardRarity::HyperRare));
    EXPECT_LT(static_cast<int>(CardRarity::HyperRare), static_cast<int>(CardRarity::Promo));

    // The whole legacy group sorts after the whole modern group.
    EXPECT_LT(static_cast<int>(CardRarity::Promo), static_cast<int>(CardRarity::RareHolo));

    // Legacy group, in ascending declaration order.
    EXPECT_LT(static_cast<int>(CardRarity::RareHolo), static_cast<int>(CardRarity::RareHoloEX));
    EXPECT_LT(static_cast<int>(CardRarity::RareHoloEX), static_cast<int>(CardRarity::RarePrime));
    EXPECT_LT(static_cast<int>(CardRarity::RarePrime), static_cast<int>(CardRarity::RareLegend));
    EXPECT_LT(static_cast<int>(CardRarity::RareLegend), static_cast<int>(CardRarity::AmazingRare));
    EXPECT_LT(static_cast<int>(CardRarity::AmazingRare), static_cast<int>(CardRarity::Shining));
    EXPECT_LT(static_cast<int>(CardRarity::Shining), static_cast<int>(CardRarity::Radiant));
    EXPECT_LT(static_cast<int>(CardRarity::Radiant), static_cast<int>(CardRarity::AceSpec));
}

}  // namespace
