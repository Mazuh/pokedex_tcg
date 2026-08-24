#include <gtest/gtest.h>

#include <iterator>

#include "core/domain/card_foil.h"

namespace {

using pokedex::CardFoil;
using pokedex::foilIsRetired;
using pokedex::kAllFoils;

// kAllFoils is the ONE list the codecs, the picker and these tests read, so it has to BE
// the enum: same values, same order, none missing. The rarity counterpart's docstring
// explains what a missing value would cost.
TEST(CardFoilTest, TheCanonicalListIsTheEnumInDeclarationOrder) {
    for (std::size_t i = 0; i < std::size(kAllFoils); ++i) {
        EXPECT_EQ(kAllFoils[i], static_cast<CardFoil>(i))
            << "kAllFoils[" << i << "] is out of declaration order or a value is missing";
    }
}

// NonHolo is deliberately first as the plain default, and the retired values are last so
// the offered options keep a contiguous sort-rank range.
TEST(CardFoilTest, NonHoloIsFirstAndRetiredValuesAreLast) {
    EXPECT_EQ(kAllFoils[0], CardFoil::NonHolo);
    EXPECT_FALSE(foilIsRetired(CardFoil::NonHolo));

    bool retiredSeen = false;
    for (const CardFoil foil : kAllFoils) {
        if (foilIsRetired(foil)) {
            retiredSeen = true;
        } else {
            EXPECT_FALSE(retiredSeen) << "an offered foil is declared after a retired one";
        }
    }
    EXPECT_TRUE(retiredSeen);
}

// Other/Unknown is an OFFERED value, not a retired one: it means "recorded, and it is
// none of the listed treatments", which is a different statement from leaving the field
// unset. Confusing the two would take the option out of the picker.
TEST(CardFoilTest, OtherFoilIsOfferedAndHDHoloIsNot) {
    EXPECT_FALSE(foilIsRetired(CardFoil::OtherFoil));
    EXPECT_TRUE(foilIsRetired(CardFoil::HDHolo));
}

}  // namespace
