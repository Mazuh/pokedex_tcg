#include <gtest/gtest.h>

#include "core/domain/card_foil.h"

namespace {

using pokedex::CardFoil;

// The foil picker lists these in declaration order and storage sort keys rank by
// it; NonHolo is deliberately first as the plain default. Pin the order so a
// reorder is caught.
TEST(CardFoilTest, IsInDeclarationOrderWithNonHoloFirst) {
    EXPECT_LT(static_cast<int>(CardFoil::NonHolo), static_cast<int>(CardFoil::Holo));
    EXPECT_LT(static_cast<int>(CardFoil::Holo), static_cast<int>(CardFoil::ReverseHolo));
    EXPECT_LT(static_cast<int>(CardFoil::ReverseHolo), static_cast<int>(CardFoil::CosmosHolo));
    EXPECT_LT(static_cast<int>(CardFoil::CosmosHolo), static_cast<int>(CardFoil::MirrorHolo));
    EXPECT_LT(static_cast<int>(CardFoil::MirrorHolo), static_cast<int>(CardFoil::CrackedIceHolo));
    EXPECT_LT(static_cast<int>(CardFoil::CrackedIceHolo),
              static_cast<int>(CardFoil::ConfettiHolo));
    EXPECT_LT(static_cast<int>(CardFoil::ConfettiHolo),
              static_cast<int>(CardFoil::CrosshatchHolo));
    EXPECT_LT(static_cast<int>(CardFoil::CrosshatchHolo), static_cast<int>(CardFoil::HDHolo));
    EXPECT_LT(static_cast<int>(CardFoil::HDHolo), static_cast<int>(CardFoil::Textured));
}

}  // namespace
