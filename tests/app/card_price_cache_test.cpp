#include "core/app/card_price_cache.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardPrice;
using pokedex::CardPriceCache;
using pokedex::Database;
using pokedex::kManualPriceProvenance;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

CardPrice makePrice(std::string id, std::string key, std::string provenance, std::string variant,
                    std::string metric, long long cents, std::string currency,
                    const char* observed) {
    CardPrice p;
    p.id = std::move(id);
    p.externalCardId = std::move(key);
    p.provenance = std::move(provenance);
    p.variant = std::move(variant);
    p.metric = std::move(metric);
    p.amountCents = cents;
    p.currency = std::move(currency);
    p.observedAt = at(observed);
    return p;
}

TEST(CardPriceCacheTest, EmptyBeforeAnyStore) {
    Database db(":memory:");
    db.migrate();
    CardPriceCache cache(db);

    EXPECT_FALSE(cache.fetchedAt("base1-4").has_value());
    EXPECT_TRUE(cache.pricesFor("base1-4").empty());
}

TEST(CardPriceCacheTest, StoreApiPricesRoundTripsFieldsAndStampsFetch) {
    Database db(":memory:");
    db.migrate();
    CardPriceCache cache(db);

    const std::vector<CardPrice> prices = {
        makePrice("id1", "base1-4", "tcgplayer", "holofoil", "market", 80043, "USD",
                  "2026-07-25T00:00:00Z"),
        makePrice("id2", "base1-4", "cardmarket", "", "trendPrice", 418460, "EUR",
                  "2026-07-01T00:00:00Z"),
    };
    cache.storeApiPrices("base1-4", prices, at("2026-07-25T12:00:00Z"));

    ASSERT_TRUE(cache.fetchedAt("base1-4").has_value());
    EXPECT_EQ(*cache.fetchedAt("base1-4"), at("2026-07-25T12:00:00Z"));

    const std::vector<CardPrice> loaded = cache.pricesFor("base1-4");
    ASSERT_EQ(loaded.size(), 2u);
    // Ordered by (provenance, variant, metric): cardmarket sorts before tcgplayer.
    EXPECT_EQ(loaded[0].provenance, "cardmarket");
    EXPECT_EQ(loaded[0].amountCents, 418460);
    EXPECT_EQ(loaded[0].currency, "EUR");
    EXPECT_EQ(loaded[0].observedAt, at("2026-07-01T00:00:00Z"));
    EXPECT_EQ(loaded[1].provenance, "tcgplayer");
    EXPECT_EQ(loaded[1].variant, "holofoil");
    EXPECT_EQ(loaded[1].metric, "market");
    EXPECT_EQ(loaded[1].amountCents, 80043);
}

TEST(CardPriceCacheTest, RefetchReplacesApiRowsButKeepsManual) {
    Database db(":memory:");
    db.migrate();
    CardPriceCache cache(db);

    cache.storeApiPrices("base1-4",
                         {makePrice("id1", "base1-4", "tcgplayer", "holofoil", "market", 80043,
                                    "USD", "2026-07-25T00:00:00Z")},
                         at("2026-07-25T12:00:00Z"));
    cache.add(makePrice("m1", "base1-4", kManualPriceProvenance, "", "", 5000, "USD",
                        "2026-07-25T13:00:00Z"));

    // A later fetch replaces the API row with a new market value; the manual survives.
    cache.storeApiPrices("base1-4",
                         {makePrice("id2", "base1-4", "tcgplayer", "holofoil", "market", 90000,
                                    "USD", "2026-07-26T00:00:00Z")},
                         at("2026-07-26T12:00:00Z"));

    const std::vector<CardPrice> loaded = cache.pricesFor("base1-4");
    ASSERT_EQ(loaded.size(), 2u);
    // manual (provenance "manual") sorts before tcgplayer.
    EXPECT_EQ(loaded[0].provenance, kManualPriceProvenance);
    EXPECT_EQ(loaded[0].amountCents, 5000);
    EXPECT_EQ(loaded[1].provenance, "tcgplayer");
    EXPECT_EQ(loaded[1].amountCents, 90000);  // the fresh value, not the stale 80043
    EXPECT_EQ(*cache.fetchedAt("base1-4"), at("2026-07-26T12:00:00Z"));
}

TEST(CardPriceCacheTest, StoreEmptyClearsApiRowsButRecordsFetch) {
    Database db(":memory:");
    db.migrate();
    CardPriceCache cache(db);

    cache.storeApiPrices("base1-4",
                         {makePrice("id1", "base1-4", "tcgplayer", "holofoil", "market", 80043,
                                    "USD", "2026-07-25T00:00:00Z")},
                         at("2026-07-25T12:00:00Z"));
    cache.storeApiPrices("base1-4", {}, at("2026-07-26T12:00:00Z"));

    EXPECT_TRUE(cache.pricesFor("base1-4").empty());
    ASSERT_TRUE(cache.fetchedAt("base1-4").has_value());
    EXPECT_EQ(*cache.fetchedAt("base1-4"), at("2026-07-26T12:00:00Z"));
}

TEST(CardPriceCacheTest, PricesAreScopedToTheirExternalCardId) {
    Database db(":memory:");
    db.migrate();
    CardPriceCache cache(db);

    cache.add(makePrice("a", "base1-4", kManualPriceProvenance, "", "", 100, "USD",
                        "2026-07-25T00:00:00Z"));
    cache.add(makePrice("b", "sv3-125", kManualPriceProvenance, "", "", 200, "USD",
                        "2026-07-25T00:00:00Z"));

    ASSERT_EQ(cache.pricesFor("base1-4").size(), 1u);
    EXPECT_EQ(cache.pricesFor("base1-4")[0].amountCents, 100);
    ASSERT_EQ(cache.pricesFor("sv3-125").size(), 1u);
    EXPECT_EQ(cache.pricesFor("sv3-125")[0].amountCents, 200);
}

TEST(CardPriceCacheTest, RemoveManualDeletesOnlyManualRows) {
    Database db(":memory:");
    db.migrate();
    CardPriceCache cache(db);

    cache.storeApiPrices("base1-4",
                         {makePrice("api1", "base1-4", "tcgplayer", "holofoil", "market", 80043,
                                    "USD", "2026-07-25T00:00:00Z")},
                         at("2026-07-25T12:00:00Z"));
    cache.add(makePrice("m1", "base1-4", kManualPriceProvenance, "", "", 5000, "USD",
                        "2026-07-25T13:00:00Z"));

    // Removing the manual id drops it; targeting an API row's id is a no-op.
    cache.removeManual("api1");
    EXPECT_EQ(cache.pricesFor("base1-4").size(), 2u);

    cache.removeManual("m1");
    const std::vector<CardPrice> loaded = cache.pricesFor("base1-4");
    ASSERT_EQ(loaded.size(), 1u);
    EXPECT_EQ(loaded[0].provenance, "tcgplayer");
}

}  // namespace
