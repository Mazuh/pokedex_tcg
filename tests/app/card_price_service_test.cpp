#include "core/app/card_price_service.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/app/card_catalog_parse.h"
#include "core/app/card_price_cache.h"
#include "core/app/card_price_dto.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardCatalogParseError;
using pokedex::CardPrice;
using pokedex::CardPriceCache;
using pokedex::CardPriceError;
using pokedex::CardPriceService;
using pokedex::Database;
using pokedex::kManualPriceProvenance;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

// A service wired to an in-memory DB with a controllable clock and deterministic
// ids, so every timestamp/id in an assertion is exact.
struct Fixture {
    Database db{":memory:"};
    CardPriceCache cache{db};
    Timestamp now = at("2026-07-25T12:00:00Z");
    int nextId = 0;
    CardPriceService service{cache, [this] { return now; },
                             [this] { return "id-" + std::to_string(++nextId); }};
    Fixture() { db.migrate(); }
};

// A minimal single-card payload with one tcgplayer and one cardmarket price.
constexpr const char* kPayload = R"json({
  "data": {
    "id": "base1-4",
    "tcgplayer":  {"updatedAt": "2026/07/20", "prices": {"holofoil": {"market": 800.43}}},
    "cardmarket": {"updatedAt": "2026/07/18", "prices": {"trendPrice": 4184.6}}
  }
})json";

TEST(CardPriceServiceTest, RecordApiPricesParsesPersistsAndStampsFetch) {
    Fixture f;
    const auto recorded = f.service.recordApiPrices("base1-4", kPayload);
    EXPECT_FALSE(recorded.degraded);
    const std::vector<CardPrice>& stored = recorded.stored;

    ASSERT_EQ(stored.size(), 2u);
    for (const CardPrice& p : stored) {
        EXPECT_EQ(p.externalCardId, "base1-4");
        EXPECT_FALSE(p.id.empty());  // minted by the service
    }
    // Persisted and readable back; fetch stamped to now().
    EXPECT_EQ(f.service.pricesFor("base1-4").size(), 2u);
    ASSERT_TRUE(f.service.fetchedAt("base1-4").has_value());
    EXPECT_EQ(*f.service.fetchedAt("base1-4"), f.now);
}

TEST(CardPriceServiceTest, RecordApiPricesReKeysToTheRequestedId) {
    Fixture f;
    // The payload's own id disagrees with the id we fetched; the requested key wins.
    constexpr const char* mismatched = R"json({
      "data": {"id": "WRONG", "tcgplayer": {"prices": {"normal": {"market": 1.0}}}}
    })json";
    f.service.recordApiPrices("sv3-125", mismatched);

    EXPECT_TRUE(f.service.pricesFor("WRONG").empty());
    ASSERT_EQ(f.service.pricesFor("sv3-125").size(), 1u);
    EXPECT_EQ(f.service.pricesFor("sv3-125")[0].externalCardId, "sv3-125");
}

TEST(CardPriceServiceTest, RecordApiPricesPreservesManualAcrossRefetch) {
    Fixture f;
    f.service.addManualPrice("base1-4", 5000, "USD", "paid at a con");
    f.service.recordApiPrices("base1-4", kPayload);

    const std::vector<CardPrice> loaded = f.service.pricesFor("base1-4");
    ASSERT_EQ(loaded.size(), 3u);  // 1 manual + 2 API
    int manual = 0;
    for (const CardPrice& p : loaded) {
        if (p.provenance == kManualPriceProvenance) {
            ++manual;
        }
    }
    EXPECT_EQ(manual, 1);
}

// A card that comes back WITH a card object but NO price blocks (a delisted card, or a
// set the API hasn't priced yet) is a real "no prices" answer: it clears the stale API
// rows and re-stamps the fetch, caching the blank. This is what makes Refresh able to
// reflect a card that lost its prices — the user's explicit insist.
TEST(CardPriceServiceTest, CardPresentWithNoPricesClearsStalePrices) {
    Fixture f;
    f.service.recordApiPrices("base1-4", kPayload);
    ASSERT_EQ(f.service.pricesFor("base1-4").size(), 2u);

    f.now = at("2026-07-26T00:00:00Z");
    const auto result = f.service.recordApiPrices(
        "base1-4", R"({"data": {"id": "base1-4", "name": "Charizard"}})");
    EXPECT_TRUE(result.stored.empty());
    EXPECT_FALSE(result.degraded);  // card present, just price-less → a real answer
    EXPECT_TRUE(f.service.pricesFor("base1-4").empty());  // cleared — the card has no prices now
    ASSERT_TRUE(f.service.fetchedAt("base1-4").has_value());
    EXPECT_EQ(*f.service.fetchedAt("base1-4"), f.now);  // re-stamped to the fresh fetch
}

// A manual price survives a card-present-empty fetch: only the API rows are cleared.
TEST(CardPriceServiceTest, CardPresentWithNoPricesKeepsManualRows) {
    Fixture f;
    f.service.recordApiPrices("base1-4", kPayload);
    f.service.addManualPrice("base1-4", 5000, "USD", "paid at a con");
    ASSERT_EQ(f.service.pricesFor("base1-4").size(), 3u);  // 2 API + 1 manual

    const auto result = f.service.recordApiPrices(
        "base1-4", R"({"data": {"id": "base1-4", "name": "Charizard"}})");
    EXPECT_TRUE(result.stored.empty());
    EXPECT_FALSE(result.degraded);
    const auto loaded = f.service.pricesFor("base1-4");
    ASSERT_EQ(loaded.size(), 1u);  // the API rows cleared, the manual one kept
    EXPECT_EQ(loaded.front().provenance, kManualPriceProvenance);
}

// A *degraded* response — NO card object at all (an error body / data:null, e.g. a
// transport failure) — must NOT wipe prices we already hold, nor bump the fetch stamp, so
// a flaky API can't blank a good card. Only a genuine card-present answer clears (above).
TEST(CardPriceServiceTest, DegradedResponseWithoutCardKeepsExistingPrices) {
    Fixture f;
    f.service.recordApiPrices("base1-4", kPayload);
    ASSERT_EQ(f.service.pricesFor("base1-4").size(), 2u);
    const auto fetchedBefore = f.service.fetchedAt("base1-4");

    f.now = at("2026-07-26T00:00:00Z");
    // Valid JSON, but no card node (a degraded/error body) — cardPresent is false.
    const auto result = f.service.recordApiPrices("base1-4", R"({"data": null})");
    EXPECT_TRUE(result.stored.empty());
    EXPECT_TRUE(result.degraded);  // no card object → failed fetch, not a "no prices" answer
    EXPECT_EQ(f.service.pricesFor("base1-4").size(), 2u);      // preserved, not wiped
    EXPECT_EQ(f.service.fetchedAt("base1-4"), fetchedBefore);  // stamp untouched → will retry
}

// A degraded response on the FIRST fetch (no prior prices) must NOT stamp either, so the
// card stays "not fetched" and the user's next Fetch/Refresh retries — rather than caching
// a false "No market prices" verdict from a transient outage.
TEST(CardPriceServiceTest, DegradedFirstFetchDoesNotStamp) {
    Fixture f;
    const auto result = f.service.recordApiPrices("sv3-5", R"({"data": null})");
    EXPECT_TRUE(result.stored.empty());
    EXPECT_TRUE(result.degraded);  // no card object → failed fetch
    EXPECT_TRUE(f.service.pricesFor("sv3-5").empty());
    EXPECT_FALSE(f.service.fetchedAt("sv3-5").has_value());  // never stamped → still fetchable
}

// A FIRST fetch that legitimately returns no prices still stamps, so a genuinely
// price-less card reads "no prices" rather than re-offering Fetch forever.
TEST(CardPriceServiceTest, FirstFetchWithNoPricesStillStamps) {
    Fixture f;
    const auto result =
        f.service.recordApiPrices("sv3-5", R"({"data": {"id": "sv3-5", "name": "No Prices"}})");
    EXPECT_TRUE(result.stored.empty());
    EXPECT_FALSE(result.degraded);  // card present, just price-less → stamp and cache the blank
    EXPECT_TRUE(f.service.pricesFor("sv3-5").empty());
    EXPECT_TRUE(f.service.fetchedAt("sv3-5").has_value());  // stamped, so it won't re-offer
}

TEST(CardPriceServiceTest, RecordApiPricesThrowsOnBadJson) {
    Fixture f;
    EXPECT_THROW(f.service.recordApiPrices("base1-4", "}{"), CardCatalogParseError);
}

TEST(CardPriceServiceTest, AddManualPriceStampsNowAndMintsId) {
    Fixture f;
    const CardPrice price = f.service.addManualPrice("base1-4", 12300, "  usd  ", "  a note  ");

    EXPECT_EQ(price.provenance, kManualPriceProvenance);
    EXPECT_EQ(price.amountCents, 12300);
    EXPECT_EQ(price.currency, "usd");  // trimmed
    EXPECT_EQ(price.note, "a note");   // trimmed
    EXPECT_EQ(price.observedAt, f.now);
    EXPECT_FALSE(price.id.empty());
    EXPECT_TRUE(price.variant.empty());
    EXPECT_TRUE(price.metric.empty());

    ASSERT_EQ(f.service.pricesFor("base1-4").size(), 1u);
}

TEST(CardPriceServiceTest, AddManualPriceRejectsNonPositiveOrBlankCurrency) {
    Fixture f;
    EXPECT_THROW(f.service.addManualPrice("base1-4", 0, "USD"), CardPriceError);
    EXPECT_THROW(f.service.addManualPrice("base1-4", -100, "USD"), CardPriceError);
    EXPECT_THROW(f.service.addManualPrice("base1-4", 100, "   "), CardPriceError);
    EXPECT_TRUE(f.service.pricesFor("base1-4").empty());
}

TEST(CardPriceServiceTest, RemoveManualPrice) {
    Fixture f;
    const CardPrice price = f.service.addManualPrice("base1-4", 100, "USD");
    ASSERT_EQ(f.service.pricesFor("base1-4").size(), 1u);

    f.service.removeManualPrice(price.id);
    EXPECT_TRUE(f.service.pricesFor("base1-4").empty());
}

}  // namespace
