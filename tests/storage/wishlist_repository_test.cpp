#include "core/storage/wishlist_repository.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/domain/wishlist.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::Database;
using pokedex::PokemonDexNum;
using pokedex::StorageError;
using pokedex::Timestamp;
using pokedex::Wishlist;
using pokedex::WishlistRepository;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

Wishlist makeWish(PokemonDexNum dex, std::set<std::string> sources) {
    Wishlist w;
    w.pokemonDexNum = dex;
    w.sources = std::move(sources);
    w.insertedAt = at("2026-07-14T09:00:00Z");
    w.updatedAt = at("2026-07-14T09:00:00Z");
    return w;
}

TEST(WishlistRepositoryTest, AddThenWishedDexNumsReturnsSpeciesWithSources) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);

    repo.add(makeWish(25, {"ebay", "https://tcgplayer.com/x"}));
    repo.add(makeWish(6, {"local shop"}));

    auto wished = repo.wishedDexNums();
    std::sort(wished.begin(), wished.end());
    EXPECT_EQ(wished, (std::vector<PokemonDexNum>{6, 25}));
}

// A Pokémon is "Wished" only with >=1 source, so a sourceless wishlist row must
// not surface in wishedDexNums (the query is over wishlist_source).
TEST(WishlistRepositoryTest, WishlistWithNoSourcesIsNotWished) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);

    repo.add(makeWish(25, {}));

    EXPECT_TRUE(repo.wishedDexNums().empty());
}

// A duplicate add fails on the wishlist primary key; the transaction rolls back
// and the original row's sources are left intact (not corrupted or dropped).
TEST(WishlistRepositoryTest, DuplicateAddThrowsAndLeavesOriginalIntact) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);
    repo.add(makeWish(25, {"ebay"}));

    EXPECT_THROW(repo.add(makeWish(25, {"a different shop"})), StorageError);

    EXPECT_EQ(repo.wishedDexNums(), (std::vector<PokemonDexNum>{25}));
}

}  // namespace
