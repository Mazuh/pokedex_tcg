#include <gtest/gtest.h>

#include <iterator>
#include <set>

#include "core/domain/card_rarity.h"

namespace {

using pokedex::CardRarity;
using pokedex::CardRarityGroup;
using pokedex::kAllRarities;
using pokedex::rarityGroup;

// kAllRarities is the ONE list the codecs, the picker and these tests read, so it has to
// BE the enum: same values, same order, none missing. A value declared but left out of
// the array would silently vanish from the picker (and be wiped from any copy carrying it
// on the next save), which is exactly the failure this pins.
TEST(CardRarityTest, TheCanonicalListIsTheEnumInDeclarationOrder) {
    for (std::size_t i = 0; i < std::size(kAllRarities); ++i) {
        EXPECT_EQ(kAllRarities[i], static_cast<CardRarity>(i))
            << "kAllRarities[" << i << "] is out of declaration order or a value is missing";
    }
}

// Groups are declared CONTIGUOUSLY: the picker emits a heading whenever the group changes
// while walking kAllRarities, so a value declared away from its group would produce a
// second heading for a group already shown.
TEST(CardRarityTest, GroupsAreContiguousAndRetiredComesLast) {
    // A std::set rather than a fixed-size bool array indexed by the group: the point of
    // this whole enum is that it keeps growing, and a hardcoded cardinality would index
    // out of bounds on a seventh group — undefined behaviour that might well still pass.
    std::set<CardRarityGroup> seen;
    for (std::size_t i = 0; i < std::size(kAllRarities); ++i) {
        const CardRarityGroup group = rarityGroup(kAllRarities[i]);
        if (i > 0 && group == rarityGroup(kAllRarities[i - 1])) {
            continue;  // still inside the same run
        }
        EXPECT_TRUE(seen.insert(group).second)
            << "group of kAllRarities[" << i << "] resumes after another group intervened";
    }

    // Retired values sort last, so the offered options keep a contiguous rank range.
    EXPECT_EQ(rarityGroup(kAllRarities[std::size(kAllRarities) - 1]), CardRarityGroup::Retired);
}

// The order the Rarity column sorts by and the picker lists in: Core first, then the
// modern premium line, the set-specific rarities, the legacy eras, and Promo. These are
// categorical branches, NOT a best-to-worst ladder — the test pins the grouping, not a
// claim about which card is scarcer.
TEST(CardRarityTest, GroupsRunFromCoreToPromo) {
    EXPECT_EQ(rarityGroup(CardRarity::Common), CardRarityGroup::Core);
    EXPECT_EQ(rarityGroup(CardRarity::DoubleRare), CardRarityGroup::Modern);
    EXPECT_EQ(rarityGroup(CardRarity::MegaHyperRare), CardRarityGroup::Modern);
    EXPECT_EQ(rarityGroup(CardRarity::HyperRare), CardRarityGroup::Special);
    EXPECT_EQ(rarityGroup(CardRarity::FuturisticRare), CardRarityGroup::Special);
    EXPECT_EQ(rarityGroup(CardRarity::RareHoloGX), CardRarityGroup::Legacy);
    EXPECT_EQ(rarityGroup(CardRarity::Promo), CardRarityGroup::Promo);
    EXPECT_EQ(rarityGroup(CardRarity::Shining), CardRarityGroup::Retired);

    EXPECT_LT(static_cast<int>(CardRarity::Rare), static_cast<int>(CardRarity::DoubleRare));
    EXPECT_LT(static_cast<int>(CardRarity::MegaHyperRare), static_cast<int>(CardRarity::AceSpec));
    EXPECT_LT(static_cast<int>(CardRarity::FuturisticRare), static_cast<int>(CardRarity::RareHolo));
    EXPECT_LT(static_cast<int>(CardRarity::RareHoloStar), static_cast<int>(CardRarity::Promo));
}

}  // namespace
