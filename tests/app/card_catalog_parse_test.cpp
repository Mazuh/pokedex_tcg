#include "core/app/card_catalog_parse.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/app/card_catalog_dto.h"

namespace {

using pokedex::CardCandidate;
using pokedex::CardCatalogParseError;
using pokedex::CardSetInfo;
using pokedex::parseCardSearchResponse;
using pokedex::parseSetsResponse;
using pokedex::resolveSetFilterToIds;

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

TEST(ParseCardSearchResponseTest, ThrowsOnInvalidJsonButEmptyDataIsFine) {
    EXPECT_THROW(parseCardSearchResponse("}{", sampleSets()), CardCatalogParseError);
    EXPECT_TRUE(parseCardSearchResponse(R"({"data": []})", sampleSets()).empty());
}

}  // namespace
