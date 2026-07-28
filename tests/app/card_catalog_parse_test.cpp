#include "core/app/card_catalog_parse.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/app/card_price_dto.h"
#include "core/storage/codecs.h"

namespace {

using pokedex::CardCandidate;
using pokedex::CardCatalogParseError;
using pokedex::CardPrice;
using pokedex::CardSetInfo;
using pokedex::parseCardSearchResponse;
using pokedex::parseSetsResponse;
using pokedex::resolveSetFilterToIds;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

// Find the one price row matching (provenance, variant, metric); asserts it exists.
const CardPrice& findPrice(const std::vector<CardPrice>& prices, const std::string& provenance,
                           const std::string& variant, const std::string& metric) {
    for (const CardPrice& p : prices) {
        if (p.provenance == provenance && p.variant == variant && p.metric == metric) {
            return p;
        }
    }
    ADD_FAILURE() << "no price for " << provenance << "/" << variant << "/" << metric;
    static const CardPrice kNone;
    return kNone;
}

// A /v2/sets payload exercising: a normal set, a duplicated printed code shared
// by two sets (CEL), a set whose ptcgoCode is null, and a malformed entry with
// no id (must be skipped, since id is the lookup key).
constexpr const char* kSetsJson = R"json({
  "data": [
    {"id": "sv3",    "name": "Obsidian Flames", "ptcgoCode": "OBF", "printedTotal": 197},
    {"id": "sv3pt5", "name": "151",             "ptcgoCode": "MEW", "printedTotal": 165},
    {"id": "cel25",  "name": "Celebrations",    "ptcgoCode": "CEL", "printedTotal": 25},
    {"id": "cel25c", "name": "Celebrations: Classic Collection", "ptcgoCode": "CEL", "printedTotal": 25},
    {"id": "pop1",   "name": "POP Series 1",    "ptcgoCode": null,  "printedTotal": 17},
    {"name": "No Id Set", "ptcgoCode": "NIS"}
  ]
})json";

std::vector<CardSetInfo> sampleSets() { return parseSetsResponse(kSetsJson); }

const CardSetInfo* findSet(const std::vector<CardSetInfo>& sets, const std::string& id) {
    for (const CardSetInfo& s : sets) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

TEST(ParseSetsResponseTest, ParsesFieldsAndSkipsEntriesWithoutId) {
    const std::vector<CardSetInfo> sets = sampleSets();
    ASSERT_EQ(sets.size(), 5u);  // six entries, the id-less one dropped

    const CardSetInfo* obf = findSet(sets, "sv3");
    ASSERT_NE(obf, nullptr);
    EXPECT_EQ(obf->ptcgoCode, "OBF");
    EXPECT_EQ(obf->name, "Obsidian Flames");
    EXPECT_EQ(obf->printedTotal, 197);
}

TEST(ParseSetsResponseTest, NullPtcgoCodeBecomesEmpty) {
    const std::vector<CardSetInfo> sets = sampleSets();
    const CardSetInfo* pop = findSet(sets, "pop1");
    ASSERT_NE(pop, nullptr);
    EXPECT_TRUE(pop->ptcgoCode.empty());
}

TEST(ParseSetsResponseTest, ThrowsOnInvalidJson) {
    EXPECT_THROW(parseSetsResponse("not json {"), CardCatalogParseError);
}

TEST(ParseSetsResponseTest, MissingDataArrayYieldsNoSets) {
    EXPECT_TRUE(parseSetsResponse("{}").empty());
    EXPECT_TRUE(parseSetsResponse(R"({"error": {"message": "bad"}})").empty());
}

TEST(ResolveSetFilterToIdsTest, MatchesAnExactCodeCaseInsensitivelyAndTrims) {
    const std::vector<CardSetInfo> sets = sampleSets();
    EXPECT_EQ(resolveSetFilterToIds("OBF", sets), std::vector<std::string>{"sv3"});
    EXPECT_EQ(resolveSetFilterToIds("obf", sets), std::vector<std::string>{"sv3"});
    EXPECT_EQ(resolveSetFilterToIds("  MEW  ", sets), std::vector<std::string>{"sv3pt5"});
}

TEST(ResolveSetFilterToIdsTest, ReturnsEveryIdForADuplicatedCode) {
    const std::vector<CardSetInfo> sets = sampleSets();
    EXPECT_EQ(resolveSetFilterToIds("CEL", sets),
              (std::vector<std::string>{"cel25", "cel25c"}));
}

// The whole point of task 7: a code-less set (POP Series 1 here, like McDonald's)
// is reachable only by a substring of its NAME, since it has no printed code.
TEST(ResolveSetFilterToIdsTest, MatchesASubstringOfTheSetName) {
    const std::vector<CardSetInfo> sets = sampleSets();
    EXPECT_EQ(resolveSetFilterToIds("pop", sets), std::vector<std::string>{"pop1"});
    // "cel" matches both the CEL code and the "Celebrations" names — still the two
    // CEL sets, each matched once.
    EXPECT_EQ(resolveSetFilterToIds("celebrations", sets),
              (std::vector<std::string>{"cel25", "cel25c"}));
}

TEST(ResolveSetFilterToIdsTest, UnknownOrBlankFilterYieldsNothing) {
    const std::vector<CardSetInfo> sets = sampleSets();
    EXPECT_TRUE(resolveSetFilterToIds("zzz", sets).empty());
    EXPECT_TRUE(resolveSetFilterToIds("", sets).empty());
    EXPECT_TRUE(resolveSetFilterToIds("   ", sets).empty());
}

// A 1-2 char fragment must NOT trigger name-substring matching (it would match a
// huge fraction of sets); exact-code matching still works at any length.
TEST(ResolveSetFilterToIdsTest, ShortFragmentDoesNotNameMatchButExactCodeStillDoes) {
    const std::vector<CardSetInfo> sets = sampleSets();
    // "ce" is a substring of "Celebrations" but is only 2 chars → no name match, and
    // no set has the exact code "ce" → nothing.
    EXPECT_TRUE(resolveSetFilterToIds("ce", sets).empty());
    // A real 2-char exact code would still resolve (none in the sample; assert the
    // 3-char name path re-enables matching).
    EXPECT_EQ(resolveSetFilterToIds("cel", sets),
              (std::vector<std::string>{"cel25", "cel25c"}));
}

// A /v2/cards payload exercising the mapping edge cases:
//  - a card whose set is in the table AND whose embedded ptcgoCode is null:
//    the code must come from the table (OBF), not the embedded null;
//  - a promo with a non-numeric collector number and a set NOT in the table:
//    fall back to the embedded set;
//  - a bare card with a resolvable set but no images/rarity/artist;
//  - a card whose set is unknown everywhere: blank code, number-only collector.
constexpr const char* kCardsJson = R"json({
  "data": [
    {
      "id": "sv3-125", "name": "Charizard ex", "number": "125",
      "rarity": "Double Rare", "artist": "Ryuta Fuse",
      "images": {"small": "https://img/small.png", "large": "https://img/large.png"},
      "set": {"id": "sv3", "name": "Obsidian Flames (embedded)", "ptcgoCode": null, "printedTotal": 0}
    },
    {
      "id": "svp-001", "name": "Pikachu Promo", "number": "SWSH001",
      "set": {"id": "promoX", "name": "SWSH Black Star Promos", "ptcgoCode": "PR", "printedTotal": 50}
    },
    {
      "id": "sv3-7", "name": "Bare Card", "number": "7",
      "set": {"id": "sv3"}
    },
    {
      "id": "ghost-12", "name": "No Total", "number": "12",
      "set": {"id": "ghost"}
    }
  ]
})json";

TEST(ParseCardSearchResponseTest, ResolvesCodeAndTotalFromTableNotEmbeddedSet) {
    const std::vector<CardCandidate> cards = parseCardSearchResponse(kCardsJson, sampleSets());
    ASSERT_EQ(cards.size(), 4u);

    const CardCandidate& charizard = cards[0];
    EXPECT_EQ(charizard.id, "sv3-125");
    EXPECT_EQ(charizard.name, "Charizard ex");
    EXPECT_EQ(charizard.cardRef.expansionCode, "OBF");  // from table, not embedded null
    EXPECT_EQ(charizard.cardRef.collectorNumber, "125/197");  // total from table
    EXPECT_EQ(charizard.setName, "Obsidian Flames");  // table name, not embedded
    EXPECT_EQ(charizard.cardRef.setName, "Obsidian Flames");  // carried into the reference
    EXPECT_EQ(charizard.cardRef.name, "Charizard ex");  // printed name carried into the reference
    EXPECT_TRUE(charizard.cardRef.language.empty());  // the user picks language
    EXPECT_EQ(charizard.imageUrlSmall, "https://img/small.png");
    EXPECT_EQ(charizard.imageUrlLarge, "https://img/large.png");
    EXPECT_EQ(charizard.rarity, "Double Rare");
    EXPECT_EQ(charizard.artist, "Ryuta Fuse");
    EXPECT_EQ(charizard.setId, "sv3");
}

TEST(ParseCardSearchResponseTest, FallsBackToEmbeddedSetAndKeepsNonNumericNumber) {
    const std::vector<CardCandidate> cards = parseCardSearchResponse(kCardsJson, sampleSets());
    const CardCandidate& promo = cards[1];
    EXPECT_EQ(promo.cardRef.expansionCode, "PR");  // embedded, since set is off-table
    EXPECT_EQ(promo.cardRef.collectorNumber, "SWSH001/50");  // non-numeric preserved
    EXPECT_EQ(promo.setName, "SWSH Black Star Promos");
}

TEST(ParseCardSearchResponseTest, DegradesGracefullyOnMissingOptionalFields) {
    const std::vector<CardCandidate> cards = parseCardSearchResponse(kCardsJson, sampleSets());
    const CardCandidate& bare = cards[2];
    EXPECT_EQ(bare.name, "Bare Card");
    EXPECT_EQ(bare.cardRef.expansionCode, "OBF");
    EXPECT_EQ(bare.cardRef.collectorNumber, "7/197");
    EXPECT_TRUE(bare.imageUrlSmall.empty());
    EXPECT_TRUE(bare.rarity.empty());
    EXPECT_TRUE(bare.artist.empty());
}

TEST(ParseCardSearchResponseTest, UnknownSetLeavesBlankCodeAndNumberOnlyCollector) {
    const std::vector<CardCandidate> cards = parseCardSearchResponse(kCardsJson, sampleSets());
    const CardCandidate& orphan = cards[3];
    EXPECT_TRUE(orphan.cardRef.expansionCode.empty());
    EXPECT_EQ(orphan.cardRef.collectorNumber, "12");  // no printedTotal → number only
    EXPECT_TRUE(orphan.setName.empty());
}

// The search payload embeds the same tcgplayer/cardmarket blocks as the per-card
// endpoint, so each candidate carries its prices with no extra HTTP call. A card
// without price blocks gets an empty list.
TEST(ParseCardSearchResponseTest, EmbedsPerCardPricesWhenThePayloadCarriesThem) {
    constexpr const char* json = R"json({
      "data": [
        {"id": "sv3-125", "name": "Gardevoir ex", "set": {"id": "sv3"},
         "tcgplayer":  {"updatedAt": "2026/07/20", "prices": {"holofoil": {"market": 12.5}}},
         "cardmarket": {"updatedAt": "2026/07/18", "prices": {"trendPrice": 9.99}}},
        {"id": "sv3-5", "name": "No Prices Card", "set": {"id": "sv3"}}
      ]
    })json";
    const std::vector<CardCandidate> cards = parseCardSearchResponse(json, sampleSets());
    ASSERT_EQ(cards.size(), 2u);

    // Both vendor rows rode along in the search response.
    ASSERT_EQ(cards[0].prices.size(), 2u);
    const CardPrice& tcg = findPrice(cards[0].prices, "tcgplayer", "holofoil", "market");
    EXPECT_EQ(tcg.externalCardId, "sv3-125");
    EXPECT_EQ(tcg.amountCents, 1250);
    EXPECT_EQ(tcg.currency, "USD");
    EXPECT_EQ(findPrice(cards[0].prices, "cardmarket", "", "trendPrice").amountCents, 999);

    // A card with no price blocks simply has none.
    EXPECT_TRUE(cards[1].prices.empty());
}

TEST(ParseCardSearchResponseTest, ThrowsOnInvalidJsonButEmptyDataIsFine) {
    EXPECT_THROW(parseCardSearchResponse("}{", sampleSets()), CardCatalogParseError);
    EXPECT_TRUE(parseCardSearchResponse(R"({"data": []})", sampleSets()).empty());
}

// ---- tcgdex parsers ---------------------------------------------------------

using pokedex::CardReference;
using pokedex::parseTcgdexCardPrices;
using pokedex::parseTcgdexSets;
using pokedex::resolveTcgdexCardId;

// A tcgdex /v2/en/cards/{id} payload (the card object at the root, no "data" wrapper),
// modelled on the real MEP-013 / sv03-125 responses: a standard holo printing with a
// cardmarket block (flat, EUR, plus the non-price idProduct/unit/updated fields and holo-
// split metrics that must NOT be read) and a tcgplayer block (USD, nested by finish, with a
// non-price productId inside the finish), then an oversized "jumbo" printing whose price must
// be ignored while a standard one exists.
constexpr const char* kTcgdexCard = R"json({
  "id": "sv03-125",
  "name": "Charizard ex",
  "localId": "125",
  "variants_detailed": [
    {
      "type": "holo", "size": "standard",
      "pricing": {
        "cardmarket": {
          "updated": "2026-07-27T08:02:24.393Z", "unit": "EUR", "idProduct": 725205,
          "avg": 3.86, "low": 1.99, "trend": 5.02, "avg1": 4.36, "avg7": 4.82, "avg30": 4.12,
          "avg-holo": 9.99, "trend-holo": 0
        },
        "tcgplayer": {
          "unit": "USD", "updated": "2026-07-25T08:03:39.374Z",
          "holofoil": {
            "productId": 509879, "lowPrice": 3.46, "midPrice": 6.2, "highPrice": 99,
            "marketPrice": 5.71, "directLowPrice": 4.77
          }
        }
      }
    },
    {
      "type": "holo", "size": "jumbo",
      "pricing": { "cardmarket": { "unit": "EUR", "trend": 40.0 } }
    }
  ]
})json";

TEST(ParseTcgdexPricesTest, NormalizesCardmarketMetricNamesToCanonicalVocabulary) {
    const auto prices = parseTcgdexCardPrices(kTcgdexCard, at("2000-01-01T00:00:00Z"));
    // tcgdex "trend"/"avg"/"low" → the canonical trendPrice/averageSellPrice/lowPrice the
    // display's vendorBest looks up, keyed to the payload's own id, EUR, at the vendor date.
    const CardPrice& trend = findPrice(prices, "cardmarket", "", "trendPrice");
    EXPECT_EQ(trend.amountCents, 502);
    EXPECT_EQ(trend.currency, "EUR");
    EXPECT_EQ(trend.externalCardId, "sv03-125");
    EXPECT_EQ(trend.observedAt, at("2026-07-27T00:00:00Z"));  // ISO date, midnight UTC
    EXPECT_EQ(findPrice(prices, "cardmarket", "", "averageSellPrice").amountCents, 386);
    EXPECT_EQ(findPrice(prices, "cardmarket", "", "lowPrice").amountCents, 199);
    EXPECT_EQ(findPrice(prices, "cardmarket", "", "avg7").amountCents, 482);
}

TEST(ParseTcgdexPricesTest, NormalizesTcgplayerMetricsAndKeepsThePerFinishVariant) {
    const auto prices = parseTcgdexCardPrices(kTcgdexCard, at("2000-01-01T00:00:00Z"));
    const CardPrice& market = findPrice(prices, "tcgplayer", "holofoil", "market");
    EXPECT_EQ(market.amountCents, 571);
    EXPECT_EQ(market.currency, "USD");
    EXPECT_EQ(market.observedAt, at("2026-07-25T00:00:00Z"));
    EXPECT_EQ(findPrice(prices, "tcgplayer", "holofoil", "mid").amountCents, 620);
    EXPECT_EQ(findPrice(prices, "tcgplayer", "holofoil", "low").amountCents, 346);
}

TEST(ParseTcgdexPricesTest, DoesNotReadNonPriceNumbersAsMoney) {
    const auto prices = parseTcgdexCardPrices(kTcgdexCard, at("2000-01-01T00:00:00Z"));
    // idProduct (725205) / productId (509879) are ids, not prices; the holo-split metric and
    // the whole tcgdex vocabulary outside our whitelist must be dropped, never turned into a
    // huge bogus row. So no cardmarket/tcgplayer row may exceed the real max (highPrice $99).
    for (const CardPrice& p : prices) {
        EXPECT_LE(p.amountCents, 9900) << p.provenance << "/" << p.metric;
    }
    // The "-holo" cardmarket split and the "high" tcgplayer metric are the only ones that
    // could sneak in; confirm the parser produced exactly the whitelisted metric set size.
    // cardmarket: trendPrice, averageSellPrice, lowPrice, avg1, avg7, avg30 = 6.
    // tcgplayer holofoil: low, mid, high, market, directLow = 5.
    EXPECT_EQ(prices.size(), 11u);
}

TEST(ParseTcgdexPricesTest, PrefersStandardSizeOverOversizedPrintings) {
    const auto prices = parseTcgdexCardPrices(kTcgdexCard, at("2000-01-01T00:00:00Z"));
    // The jumbo printing's €40 trend must not appear while a standard printing is priced.
    for (const CardPrice& p : prices) {
        EXPECT_NE(p.amountCents, 4000);
    }
}

TEST(ParseTcgdexPricesTest, FallsBackToOversizedWhenNoStandardPrintingIsPriced) {
    constexpr const char* kJumboOnly = R"json({
      "id": "prom-1",
      "variants_detailed": [
        { "type": "holo", "size": "jumbo",
          "pricing": { "cardmarket": { "unit": "EUR", "trend": 12.5 } } }
      ]
    })json";
    const auto prices = parseTcgdexCardPrices(kJumboOnly, at("2000-01-01T00:00:00Z"));
    ASSERT_EQ(prices.size(), 1u);
    EXPECT_EQ(prices.front().amountCents, 1250);
}

TEST(ParseTcgdexPricesTest, FallsBackToFetchTimeWhenVendorDateMissingOrMalformed) {
    constexpr const char* kNoDate = R"json({
      "id": "x-1",
      "variants_detailed": [
        { "size": "standard",
          "pricing": { "cardmarket": { "unit": "EUR", "trend": 1.0, "updated": "nope" } } }
      ]
    })json";
    const Timestamp fallback = at("2020-05-05T00:00:00Z");
    const auto prices = parseTcgdexCardPrices(kNoDate, fallback);
    ASSERT_EQ(prices.size(), 1u);
    EXPECT_EQ(prices.front().observedAt, fallback);
}

TEST(ParseTcgdexPricesTest, DropsSubCentAmountsAndYieldsNoRowsForAPricelessCard) {
    constexpr const char* kNoisy = R"json({
      "id": "x-1",
      "variants_detailed": [
        { "size": "standard", "pricing": { "cardmarket": { "unit": "EUR", "trend": 0.004 } } }
      ]
    })json";
    EXPECT_TRUE(parseTcgdexCardPrices(kNoisy, at("2000-01-01T00:00:00Z")).empty());
    // A card with no variants_detailed at all is priceless, not an error.
    EXPECT_TRUE(
        parseTcgdexCardPrices(R"({"id": "x-1"})", at("2000-01-01T00:00:00Z")).empty());
}

TEST(ParseTcgdexPricesTest, ResultReportsCardPresenceAndThrowsOnlyOnInvalidJson) {
    // A real card is present; a 404 error body (no "id") is a degraded response, not a card.
    EXPECT_TRUE(pokedex::parseTcgdexCardPricesResult(R"({"id": "x-1"})", at("2000-01-01T00:00:00Z"))
                    .cardPresent);
    EXPECT_FALSE(pokedex::parseTcgdexCardPricesResult(
                     R"({"status": 404, "title": "not found"})", at("2000-01-01T00:00:00Z"))
                     .cardPresent);
    EXPECT_THROW(parseTcgdexCardPrices("}{", at("2000-01-01T00:00:00Z")), CardCatalogParseError);
}

// ---- parseTcgdexSets --------------------------------------------------------

// tcgdex /v2/en/sets is a FLAT array (no "data" wrapper), each set carrying a nested
// cardCount and no printed code.
constexpr const char* kTcgdexSets = R"json([
  {"id": "sv03", "name": "Obsidian Flames",       "cardCount": {"total": 230, "official": 197}},
  {"id": "mep",  "name": "MEP Black Star Promos",  "cardCount": {"total": 60,  "official": 0}},
  {"id": "swsh12", "name": "Silver Tempest",       "cardCount": {"total": 215}},
  {"name": "No Id Set"}
])json";

TEST(ParseTcgdexSetsTest, ParsesFlatArrayCarriesTotalAndSkipsIdlessEntries) {
    const auto sets = parseTcgdexSets(kTcgdexSets);
    ASSERT_EQ(sets.size(), 3u);  // the id-less entry is skipped
    EXPECT_EQ(sets[0].id, "sv03");
    EXPECT_EQ(sets[0].name, "Obsidian Flames");
    EXPECT_EQ(sets[0].printedTotal, 230);
    EXPECT_TRUE(sets[0].ptcgoCode.empty());  // tcgdex publishes none
}

TEST(ParseTcgdexSetsTest, NonArrayPayloadYieldsNoSets) {
    EXPECT_TRUE(parseTcgdexSets(R"({"data": []})").empty());
    EXPECT_THROW(parseTcgdexSets("}{"), CardCatalogParseError);
}

// ---- resolveTcgdexCardId ----------------------------------------------------

CardReference ref(std::string code, std::string number, std::string setName = "") {
    CardReference r;
    r.expansionCode = std::move(code);
    r.collectorNumber = std::move(number);
    r.setName = std::move(setName);
    return r;
}

TEST(ResolveTcgdexCardIdTest, MatchesPrintedCodeAsSetIdForPromos) {
    const auto sets = parseTcgdexSets(kTcgdexSets);
    // The manual MEP case: the user typed code "MEP" (== the tcgdex set id "mep"), number 013.
    EXPECT_EQ(resolveTcgdexCardId(ref("MEP", "013"), sets), "mep-013");
}

TEST(ResolveTcgdexCardIdTest, MatchesMainSetByNameNotItsUnrelatedPrintedCode) {
    const auto sets = parseTcgdexSets(kTcgdexSets);
    // A finder-added card: printed code "OBF" is nothing like the tcgdex id "sv03", but the
    // set NAME matches, and the collector number strips its "/197" total to the localId.
    EXPECT_EQ(resolveTcgdexCardId(ref("OBF", "125/197", "Obsidian Flames"), sets), "sv03-125");
}

TEST(ResolveTcgdexCardIdTest, PrefersAnExactNameMatchOverACollidingCode) {
    // A code that coincidentally equals an unrelated set's tcgdex id ("mep") must NOT win when
    // the set NAME exactly identifies a different set — name is matched first.
    std::vector<CardSetInfo> sets = parseTcgdexSets(kTcgdexSets);
    EXPECT_EQ(resolveTcgdexCardId(ref("MEP", "5", "Silver Tempest"), sets), "swsh12-5");
}

TEST(ResolveTcgdexCardIdTest, RefusesFuzzyMatchesAmbiguityAndUnknowns) {
    const auto sets = parseTcgdexSets(kTcgdexSets);
    // A loose substring is NOT trusted (only exact name / code-as-id) — refuse rather than
    // risk pricing the wrong card.
    EXPECT_FALSE(resolveTcgdexCardId(ref("", "5", "silver"), sets).has_value());
    // No collector number → nothing to address.
    EXPECT_FALSE(resolveTcgdexCardId(ref("MEP", "  "), sets).has_value());
    // An unidentifiable set → refuse rather than guess a wrong card.
    EXPECT_FALSE(resolveTcgdexCardId(ref("ZZZ", "1", "Totally Unknown Set"), sets).has_value());
}

}  // namespace
