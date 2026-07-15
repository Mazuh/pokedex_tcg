#include "core/storage/codecs.h"

#include <gtest/gtest.h>

#include <chrono>

#include "core/domain/region.h"
#include "core/storage/database.h"

namespace {

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
