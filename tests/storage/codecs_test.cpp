#include <optional>

#include "core/storage/codecs.h"

#include <gtest/gtest.h>

#include <chrono>

#include "core/domain/card_condition.h"
#include "core/domain/card_foil.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_rarity.h"
#include "core/domain/region.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardCondition;
using pokedex::CardFoil;
using pokedex::CardOwnership;
using pokedex::CardRarity;
using pokedex::Region;
using pokedex::StorageError;
using pokedex::Timestamp;

// Every region round-trips through its storage token — the property the on-disk
// format depends on. Listed explicitly (not a loop over the enum) so a reordered
// or renamed token is caught.
TEST(CodecsTest, RegionRoundTripsForEveryValue) {
    for (const Region region : {Region::Kanto, Region::Johto, Region::Hoenn, Region::Sinnoh,
                                Region::Unova, Region::Kalos, Region::Alola, Region::Galar,
                                Region::Paldea}) {
        EXPECT_EQ(pokedex::regionFromText(pokedex::regionToText(region)), region);
    }
}

TEST(CodecsTest, RegionTokensAreTheExpectedText) {
    EXPECT_EQ(pokedex::regionToText(Region::Kanto), "Kanto");
    EXPECT_EQ(pokedex::regionToText(Region::Paldea), "Paldea");
    EXPECT_EQ(pokedex::regionFromText("Johto"), Region::Johto);
}

TEST(CodecsTest, UnknownRegionTokenThrows) {
    EXPECT_THROW(pokedex::regionFromText("Atlantis"), StorageError);
    EXPECT_THROW(pokedex::regionFromText(""), StorageError);
}

// Every ownership state round-trips through its storage token. Listed explicitly
// (not a loop) so a reordered or renamed token is caught.
TEST(CodecsTest, OwnershipRoundTripsForEveryValue) {
    for (const CardOwnership ownership :
         {CardOwnership::Incoming, CardOwnership::Owned, CardOwnership::Removed}) {
        EXPECT_EQ(pokedex::ownershipFromText(pokedex::ownershipToText(ownership)), ownership);
    }
}

TEST(CodecsTest, OwnershipTokensAreTheExpectedText) {
    EXPECT_EQ(pokedex::ownershipToText(CardOwnership::Incoming), "Incoming");
    EXPECT_EQ(pokedex::ownershipToText(CardOwnership::Owned), "Owned");
    EXPECT_EQ(pokedex::ownershipToText(CardOwnership::Removed), "Removed");
}

TEST(CodecsTest, UnknownOwnershipTokenThrows) {
    EXPECT_THROW(pokedex::ownershipFromText("Lost"), StorageError);
    EXPECT_THROW(pokedex::ownershipFromText(""), StorageError);
}

// Every condition grade round-trips through its storage token.
TEST(CodecsTest, ConditionRoundTripsForEveryValue) {
    for (const CardCondition condition :
         {CardCondition::NearMint, CardCondition::LightlyPlayed,
          CardCondition::ModeratelyPlayed, CardCondition::HeavilyPlayed,
          CardCondition::Damaged}) {
        EXPECT_EQ(pokedex::conditionFromText(pokedex::conditionToText(condition)), condition);
    }
}

TEST(CodecsTest, ConditionTokensAreTheExpectedText) {
    EXPECT_EQ(pokedex::conditionToText(CardCondition::NearMint), "NearMint");
    EXPECT_EQ(pokedex::conditionToText(CardCondition::Damaged), "Damaged");
    EXPECT_EQ(pokedex::conditionFromText("HeavilyPlayed"), CardCondition::HeavilyPlayed);
}

TEST(CodecsTest, UnknownConditionTokenThrows) {
    EXPECT_THROW(pokedex::conditionFromText("Mint"), StorageError);
}

// Condition is optional: nullopt <-> the empty string (an ungraded copy).
TEST(CodecsTest, ConditionOptionalRoundTripsThroughEmptyString) {
    EXPECT_EQ(pokedex::conditionToText(std::nullopt), "");
    EXPECT_EQ(pokedex::conditionFromText(""), std::nullopt);
}

// Every rarity round-trips through its storage token. Driven off kAllRarities rather
// than a list spelled out here, so a value added to the enum is covered automatically —
// and, since rarityFromText decodes off that same array, a value MISSING from it fails
// the domain suite's ordering test rather than silently going unread here.
TEST(CodecsTest, RarityRoundTripsForEveryValue) {
    for (const CardRarity rarity : pokedex::kAllRarities) {
        EXPECT_EQ(pokedex::rarityFromText(pokedex::rarityToText(rarity)), rarity);
    }
}

TEST(CodecsTest, RarityTokensAreTheExpectedText) {
    EXPECT_EQ(pokedex::rarityToText(CardRarity::Common), "Common");
    EXPECT_EQ(pokedex::rarityToText(CardRarity::DoubleRare), "DoubleRare");
    EXPECT_EQ(pokedex::rarityToText(CardRarity::AceSpec), "AceSpec");
    EXPECT_EQ(pokedex::rarityToText(CardRarity::MegaHyperRare), "MegaHyperRare");
    EXPECT_EQ(pokedex::rarityToText(CardRarity::HoloRareVSTAR), "HoloRareVSTAR");
    EXPECT_EQ(pokedex::rarityFromText("HyperRare"), CardRarity::HyperRare);
    EXPECT_EQ(pokedex::rarityFromText("SecretRare"), CardRarity::SecretRare);
}

TEST(CodecsTest, UnknownRarityTokenThrows) {
    EXPECT_THROW(pokedex::rarityFromText("SuperRare"), StorageError);
}

// Rarity is optional: nullopt <-> the empty string (a copy without a rarity).
TEST(CodecsTest, RarityOptionalRoundTripsThroughEmptyString) {
    EXPECT_EQ(pokedex::rarityToText(std::nullopt), "");
    EXPECT_EQ(pokedex::rarityFromText(""), std::nullopt);
}

// Every foil treatment round-trips through its storage token — driven off kAllFoils, for
// the reason the rarity case above gives.
TEST(CodecsTest, FoilRoundTripsForEveryValue) {
    for (const CardFoil foil : pokedex::kAllFoils) {
        EXPECT_EQ(pokedex::foilFromText(pokedex::foilToText(foil)), foil);
    }
}

TEST(CodecsTest, FoilTokensAreTheExpectedText) {
    EXPECT_EQ(pokedex::foilToText(CardFoil::NonHolo), "NonHolo");
    EXPECT_EQ(pokedex::foilToText(CardFoil::ReverseHolo), "ReverseHolo");
    EXPECT_EQ(pokedex::foilToText(CardFoil::Textured), "Textured");
    EXPECT_EQ(pokedex::foilToText(CardFoil::OtherFoil), "OtherFoil");
    EXPECT_EQ(pokedex::foilFromText("CosmosHolo"), CardFoil::CosmosHolo);
    EXPECT_EQ(pokedex::foilFromText("MirageHolo"), CardFoil::MirageHolo);
}

TEST(CodecsTest, UnknownFoilTokenThrows) {
    EXPECT_THROW(pokedex::foilFromText("RainbowHolo"), StorageError);
}

// THE COMPATIBILITY GUARD. Rarity and foil are stored as free-text tokens and an unknown
// one THROWS — from inside CardCopyRepository::listAll, so a single stale token fails the
// whole card-list load, not one row. Every token below is one a real database can already
// contain, so decoding each of them must keep working forever.
//
// This list may only ever GROW. If a value is withdrawn from the pickers, its enumerator
// (and therefore its token) stays; see CardRarityGroup::Retired and foilIsRetired(). A
// deletion or a rename here is a data-loss bug, not a cleanup — which is why the tokens
// are spelled as string LITERALS: writing them as rarityToText(...) would make the test
// agree with any rename instead of catching it.
TEST(CodecsTest, TokensAlreadyInDatabasesStillDecode) {
    for (const char* token :
         {"Common", "Uncommon", "Rare", "DoubleRare", "IllustrationRare", "UltraRare",
          "SpecialIllustrationRare", "HyperRare", "Promo", "RareHolo", "RareHoloEX",
          "RarePrime", "RareLegend", "AmazingRare", "Shining", "Radiant", "AceSpec"}) {
        EXPECT_NE(pokedex::rarityFromText(token), std::nullopt) << "rarity token " << token;
    }
    for (const char* token :
         {"NonHolo", "Holo", "ReverseHolo", "CosmosHolo", "MirrorHolo", "CrackedIceHolo",
          "ConfettiHolo", "CrosshatchHolo", "HDHolo", "Textured"}) {
        EXPECT_NE(pokedex::foilFromText(token), std::nullopt) << "foil token " << token;
    }
}

// Foil treatment is optional: nullopt <-> the empty string.
TEST(CodecsTest, FoilOptionalRoundTripsThroughEmptyString) {
    EXPECT_EQ(pokedex::foilToText(std::nullopt), "");
    EXPECT_EQ(pokedex::foilFromText(""), std::nullopt);
}

// The stored form matches the literals the schema tests already use.
TEST(CodecsTest, TimestampParsesTheCanonicalLiteral) {
    const Timestamp when = pokedex::timestampFromIso("2026-07-14T00:00:00Z");
    EXPECT_EQ(pokedex::timestampToIso(when), "2026-07-14T00:00:00Z");
}

TEST(CodecsTest, TimestampRoundTripsAtSecondPrecision) {
    const std::string iso = "2024-02-29T13:45:07Z";  // leap day, non-midnight
    EXPECT_EQ(pokedex::timestampToIso(pokedex::timestampFromIso(iso)), iso);
}

// Sub-second parts are truncated on encode, so a time_point carrying
// milliseconds still serializes to whole seconds.
TEST(CodecsTest, TimestampTruncatesSubSecond) {
    const Timestamp base = pokedex::timestampFromIso("2026-07-14T00:00:00Z");
    const Timestamp withMillis = base + std::chrono::milliseconds(750);
    EXPECT_EQ(pokedex::timestampToIso(withMillis), "2026-07-14T00:00:00Z");
}

TEST(CodecsTest, MalformedTimestampThrows) {
    EXPECT_THROW(pokedex::timestampFromIso("not-a-date"), StorageError);
    EXPECT_THROW(pokedex::timestampFromIso("2026-07-14"), StorageError);
    EXPECT_THROW(pokedex::timestampFromIso("2026-07-14T00:00:00Z trailing"), StorageError);
}

}  // namespace
