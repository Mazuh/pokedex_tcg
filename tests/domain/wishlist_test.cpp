#include <gtest/gtest.h>

#include "core/domain/wishlist.h"

namespace {

using pokedex::Wishlist;

TEST(WishlistTest, StartsEmpty) {
    Wishlist wishlist{};
    EXPECT_TRUE(wishlist.sources.empty());
}

TEST(WishlistTest, HoldsDedupedSourcesForAPokemon) {
    Wishlist wishlist{
        .pokemonDexNum = 2,  // Ivysaur
        .sources = {"https://market.example/ivysaur", "AuntCollector"},
    };
    wishlist.sources.insert("AuntCollector");  // duplicate ignored by the set

    EXPECT_EQ(wishlist.pokemonDexNum, 2);
    EXPECT_EQ(wishlist.sources.size(), 2u);
    EXPECT_TRUE(wishlist.sources.contains("AuntCollector"));
}

}  // namespace
