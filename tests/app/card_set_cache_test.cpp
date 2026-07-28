#include "core/app/card_set_cache.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardSetCache;
using pokedex::CardSetInfo;
using pokedex::Database;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

CardSetInfo makeSet(std::string id, std::string code, std::string name, int total) {
    CardSetInfo info;
    info.id = std::move(id);
    info.ptcgoCode = std::move(code);
    info.name = std::move(name);
    info.printedTotal = total;
    return info;
}

TEST(CardSetCacheTest, EmptyBeforeAnyStore) {
    Database db(":memory:");
    db.migrate();
    CardSetCache cache(db, "pokemontcg");

    EXPECT_FALSE(cache.fetchedAt().has_value());
    EXPECT_TRUE(cache.load().empty());
}

TEST(CardSetCacheTest, StoreThenLoadRoundTripsFieldsOrderedById) {
    Database db(":memory:");
    db.migrate();
    CardSetCache cache(db, "pokemontcg");

    const std::vector<CardSetInfo> sets = {
        makeSet("sv3", "OBF", "Obsidian Flames", 197),
        makeSet("base1", "", "Base", 102),  // a code-less set
    };
    const Timestamp when = at("2026-07-25T12:00:00Z");
    cache.store(sets, when);

    ASSERT_TRUE(cache.fetchedAt().has_value());
    EXPECT_EQ(*cache.fetchedAt(), when);

    const std::vector<CardSetInfo> loaded = cache.load();
    ASSERT_EQ(loaded.size(), 2u);
    // Ordered by id: "base1" sorts before "sv3".
    EXPECT_EQ(loaded[0].id, "base1");
    EXPECT_EQ(loaded[0].ptcgoCode, "");
    EXPECT_EQ(loaded[0].name, "Base");
    EXPECT_EQ(loaded[0].printedTotal, 102);
    EXPECT_EQ(loaded[1].id, "sv3");
    EXPECT_EQ(loaded[1].ptcgoCode, "OBF");
    EXPECT_EQ(loaded[1].name, "Obsidian Flames");
    EXPECT_EQ(loaded[1].printedTotal, 197);
}

TEST(CardSetCacheTest, StoreReplacesTheWholeTable) {
    Database db(":memory:");
    db.migrate();
    CardSetCache cache(db, "pokemontcg");

    cache.store({makeSet("sv1", "SVI", "Scarlet & Violet", 198),
                 makeSet("sv2", "PAL", "Paldea Evolved", 193)},
                at("2026-07-24T00:00:00Z"));
    // A second store is a full replacement, not a merge: the old rows are gone.
    cache.store({makeSet("sv3", "OBF", "Obsidian Flames", 197)},
                at("2026-07-25T00:00:00Z"));

    const std::vector<CardSetInfo> loaded = cache.load();
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].id, "sv3");
    EXPECT_EQ(*cache.fetchedAt(), at("2026-07-25T00:00:00Z"));
}

TEST(CardSetCacheTest, StoringEmptyClearsRowsButKeepsTimestamp) {
    Database db(":memory:");
    db.migrate();
    CardSetCache cache(db, "pokemontcg");

    cache.store({makeSet("sv3", "OBF", "Obsidian Flames", 197)},
                at("2026-07-24T00:00:00Z"));
    cache.store({}, at("2026-07-25T00:00:00Z"));

    EXPECT_TRUE(cache.load().empty());
    ASSERT_TRUE(cache.fetchedAt().has_value());
    EXPECT_EQ(*cache.fetchedAt(), at("2026-07-25T00:00:00Z"));
}

// Two providers share the one table without interfering: each source has its own rows and its
// own fetch stamp, even when they use the SAME set id string ("sv03" here) with different data.
TEST(CardSetCacheTest, SourcesAreIsolatedInTheSharedTable) {
    Database db(":memory:");
    db.migrate();
    CardSetCache pokemontcg(db, "pokemontcg");
    CardSetCache tcgdex(db, "tcgdex");

    pokemontcg.store({makeSet("sv3", "OBF", "Obsidian Flames", 197)}, at("2026-07-24T00:00:00Z"));
    tcgdex.store({makeSet("sv03", "", "Obsidian Flames", 230), makeSet("mep", "", "MEP", 60)},
                 at("2026-07-25T00:00:00Z"));

    // Each source reads back only its own rows and its own stamp.
    ASSERT_EQ(pokemontcg.load().size(), 1u);
    EXPECT_EQ(pokemontcg.load()[0].id, "sv3");
    EXPECT_EQ(*pokemontcg.fetchedAt(), at("2026-07-24T00:00:00Z"));

    ASSERT_EQ(tcgdex.load().size(), 2u);
    EXPECT_EQ(tcgdex.load()[0].id, "mep");  // ordered by id within the source
    EXPECT_EQ(*tcgdex.fetchedAt(), at("2026-07-25T00:00:00Z"));

    // Replacing one source's rows leaves the other's intact.
    tcgdex.store({makeSet("sv03", "", "Obsidian Flames", 230)}, at("2026-07-26T00:00:00Z"));
    EXPECT_EQ(pokemontcg.load().size(), 1u);
    EXPECT_EQ(tcgdex.load().size(), 1u);
}

}  // namespace
