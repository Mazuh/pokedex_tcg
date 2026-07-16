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
using pokedex::Timestamp;
using pokedex::Wishlist;
using pokedex::WishlistRepository;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

Wishlist makeWish(PokemonDexNum dex, std::set<std::string> sources,
                  const char* inserted = "2026-07-14T09:00:00Z",
                  const char* updated = "2026-07-14T09:00:00Z") {
    Wishlist w;
    w.pokemonDexNum = dex;
    w.sources = std::move(sources);
    w.insertedAt = at(inserted);
    w.updatedAt = at(updated);
    return w;
}

TEST(WishlistRepositoryTest, SaveThenWishedDexNumsReturnsSpeciesWithSources) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);

    repo.save(makeWish(25, {"ebay", "https://tcgplayer.com/x"}));
    repo.save(makeWish(6, {"local shop"}));

    auto wished = repo.wishedDexNums();
    std::sort(wished.begin(), wished.end());
    EXPECT_EQ(wished, (std::vector<PokemonDexNum>{6, 25}));
}

TEST(WishlistRepositoryTest, FindRoundTripsSourcesAndStamps) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);
    repo.save(makeWish(25, {"ebay", "https://tcgplayer.com/x"}));

    auto found = repo.find(25);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->pokemonDexNum, 25);
    EXPECT_EQ(found->sources,
              (std::set<std::string>{"ebay", "https://tcgplayer.com/x"}));
    EXPECT_EQ(found->insertedAt, at("2026-07-14T09:00:00Z"));

    EXPECT_FALSE(repo.find(6).has_value());
}

// Re-saving the same species replaces its source set wholesale and bumps
// updatedAt, but keeps the original insertedAt (upsert, not delete-then-insert).
TEST(WishlistRepositoryTest, SaveUpsertReplacesSourcesAndPreservesInsertedAt) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);
    repo.save(makeWish(25, {"ebay"}, "2026-07-14T09:00:00Z", "2026-07-14T09:00:00Z"));

    repo.save(makeWish(25, {"local shop", "https://x.test"}, "2026-07-20T00:00:00Z",
                       "2026-07-15T12:00:00Z"));

    auto found = repo.find(25);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->sources,
              (std::set<std::string>{"local shop", "https://x.test"}));
    EXPECT_EQ(found->insertedAt, at("2026-07-14T09:00:00Z"));  // original kept
    EXPECT_EQ(found->updatedAt, at("2026-07-15T12:00:00Z"));   // bumped
}

// A Pokémon is "Wished" only with >=1 source, so a sourceless wishlist row must
// not surface in wishedDexNums or listAll (both key off wishlist_source).
TEST(WishlistRepositoryTest, WishlistWithNoSourcesIsNotSurfaced) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);

    repo.save(makeWish(25, {}));

    EXPECT_TRUE(repo.wishedDexNums().empty());
    EXPECT_TRUE(repo.listAll().empty());
    EXPECT_TRUE(repo.find(25).has_value());  // the parent row still exists
}

TEST(WishlistRepositoryTest, ListAllReturnsNonEmptyWishlistsInDexOrder) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);
    repo.save(makeWish(25, {"ebay"}));
    repo.save(makeWish(6, {"shop-a", "shop-b"}));
    repo.save(makeWish(150, {}));  // sourceless — excluded

    auto all = repo.listAll();
    ASSERT_EQ(all.size(), 2u);
    EXPECT_EQ(all[0].pokemonDexNum, 6);
    EXPECT_EQ(all[0].sources, (std::set<std::string>{"shop-a", "shop-b"}));
    EXPECT_EQ(all[1].pokemonDexNum, 25);
}

TEST(WishlistRepositoryTest, RemoveDeletesParentAndCascadesSources) {
    Database db(":memory:");
    db.migrate();
    WishlistRepository repo(db);
    repo.save(makeWish(25, {"ebay", "https://x.test"}));

    repo.remove(25);

    EXPECT_FALSE(repo.find(25).has_value());
    EXPECT_TRUE(repo.wishedDexNums().empty());
    EXPECT_NO_THROW(repo.remove(25));  // removing a missing row is a no-op
}

}  // namespace
