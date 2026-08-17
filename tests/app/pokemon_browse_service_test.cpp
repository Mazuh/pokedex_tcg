#include "core/app/pokemon_browse_service.h"

#include <gtest/gtest.h>

#include <array>
#include <optional>
#include <string>
#include <unordered_map>

#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/domain/pokemon_catalog.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace {

using pokedex::CardCondition;
using pokedex::CardCopy;
using pokedex::CardCopyRepository;
using pokedex::CardOwnership;
using pokedex::CardReference;
using pokedex::Database;
using pokedex::PokemonBrowseEntry;
using pokedex::PokemonBrowseService;
using pokedex::PokemonDexNum;
using pokedex::Region;
using pokedex::RegionProgress;
using pokedex::Timestamp;
using pokedex::kRegions;

constexpr int kBulbasaur = 1;
constexpr int kPikachu = 25;
constexpr int kChikorita = 152;  // Johto — a species outside Kanto

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

CardCopy makeCopy(std::string id, PokemonDexNum dex, CardOwnership ownership,
                  std::optional<std::string> binderId) {
    CardCopy copy;
    copy.id = std::move(id);
    copy.pokemonDexNum = dex;
    copy.cardRef = CardReference{"MEW", "EN", "151/165"};
    copy.ownership = ownership;
    copy.condition = CardCondition::NearMint;
    copy.binderId = std::move(binderId);
    copy.insertedAt = at("2026-07-14T09:00:00Z");
    copy.updatedAt = at("2026-07-14T09:00:00Z");
    return copy;
}

// The owned count of a given dex number in the entries, or nullopt if absent.
std::optional<int> ownedOf(const std::vector<PokemonBrowseEntry>& entries,
                           PokemonDexNum dex) {
    for (const PokemonBrowseEntry& e : entries) {
        if (e.pokemon.dexNumber == dex) {
            return e.ownedCount;
        }
    }
    return std::nullopt;
}

// The progress row for a region, looked up BY REGION rather than by index — so an
// ordering bug fails the ordering test below instead of silently rewriting every
// other expectation here.
const RegionProgress& rowFor(const std::array<RegionProgress, kRegions.size()>& rows,
                             Region region) {
    for (const RegionProgress& row : rows) {
        if (row.region == region) {
            return row;
        }
    }
    ADD_FAILURE() << "no progress row for the requested region";
    return rows.front();
}

// The catalog's own species count for a region, tallied independently of the
// projection under test.
int catalogSpeciesIn(Region region) {
    int count = 0;
    for (const pokedex::Pokemon& pokemon : pokedex::pokemonCatalog()) {
        if (pokemon.region == region) {
            ++count;
        }
    }
    return count;
}

struct BrowseTest : ::testing::Test {
    Database db{":memory:"};
    CardCopyRepository copies{db};
    PokemonBrowseService browse{copies};

    BrowseTest() { db.migrate(); }
};

// With no copies, the list still spans the whole catalog in dex order, every
// count zero.
TEST_F(BrowseTest, EmptyCollectionListsWholeCatalogAllZero) {
    const auto entries = browse.listAll();

    ASSERT_EQ(entries.size(), pokedex::pokemonCatalog().size());
    EXPECT_EQ(entries.front().pokemon.dexNumber, kBulbasaur);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].pokemon.dexNumber, static_cast<int>(i) + 1);
        EXPECT_EQ(entries[i].ownedCount, 0);
    }
}

// Owned copies are counted per species across binders and unfiled; Incoming and
// Removed copies never count.
TEST_F(BrowseTest, CountsOwnedCopiesAcrossBindersAndExcludesNonOwned) {
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','A',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z'),"
        "       ('b2','B',NULL,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Owned, "b2"));
    copies.add(makeCopy("c3", kPikachu, CardOwnership::Owned, std::nullopt));
    copies.add(makeCopy("c4", kPikachu, CardOwnership::Incoming, "b1"));  // not owned
    copies.add(makeCopy("c5", kBulbasaur, CardOwnership::Removed, "b1"));  // not owned

    const auto entries = browse.listAll();

    EXPECT_EQ(ownedOf(entries, kPikachu), 3);      // three Owned copies
    EXPECT_EQ(ownedOf(entries, kBulbasaur), 0);    // only a Removed copy
}

// The counts-supplied overload pairs the catalog with a caller-provided map
// rather than querying — a species in the map reports its count, one absent
// reports zero, and the list still spans the whole catalog in dex order.
TEST_F(BrowseTest, ListAllFromSuppliedCountsPairsCatalogWithoutQuerying) {
    const std::unordered_map<PokemonDexNum, int> counts{{kPikachu, 4}};

    const auto entries = browse.listAll(counts);

    ASSERT_EQ(entries.size(), pokedex::pokemonCatalog().size());
    EXPECT_EQ(entries.front().pokemon.dexNumber, kBulbasaur);
    EXPECT_EQ(ownedOf(entries, kPikachu), 4);      // from the supplied map
    EXPECT_EQ(ownedOf(entries, kBulbasaur), 0);    // absent → zero
}

// The breakdown is one row per region in the canonical kRegions order, so the
// panel rendering it never has to sort, dedupe, or check for a missing region.
TEST_F(BrowseTest, RegionProgressReportsEveryRegionOnceInCanonicalOrder) {
    const auto rows = pokedex::regionProgress(browse.listAll());

    ASSERT_EQ(rows.size(), kRegions.size());
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        EXPECT_EQ(rows[i].region, kRegions[i]);
    }
}

// The denominator of each region is the catalog's species count for it — never
// whatever subset happened to be passed in. This is the anti-drift test: making
// the figures filter-scoped would fail here rather than ship a "95 of 95".
TEST_F(BrowseTest, RegionTotalsAreTheCatalogsSpeciesCountPerRegion) {
    const auto rows = pokedex::regionProgress(browse.listAll());

    int sum = 0;
    for (const Region region : kRegions) {
        const int expected = catalogSpeciesIn(region);
        EXPECT_GT(expected, 0);  // every region has species; a zero would mask a bug
        EXPECT_EQ(rowFor(rows, region).totalSpecies, expected);
        sum += expected;
    }
    EXPECT_EQ(sum, static_cast<int>(pokedex::pokemonCatalog().size()));
}

// A species is captured once however many duplicates you own; the duplicates
// only move the Cards figure.
TEST_F(BrowseTest, CapturedCountsSpeciesNotCopies) {
    const std::unordered_map<PokemonDexNum, int> counts{{kPikachu, 3}};

    const auto rows = pokedex::regionProgress(browse.listAll(counts));

    EXPECT_EQ(rowFor(rows, Region::Kanto).capturedSpecies, 1);
    EXPECT_EQ(rowFor(rows, Region::Kanto).cards, 3);
}

// The denominator is the catalog and never shrinks with ownership: an empty
// collection captures nothing while every region still reports its full total.
TEST_F(BrowseTest, ASpeciesWithNoOwnedCopyIsNeverCaptured) {
    const auto rows = pokedex::regionProgress(browse.listAll());

    for (const Region region : kRegions) {
        EXPECT_EQ(rowFor(rows, region).capturedSpecies, 0);
        EXPECT_EQ(rowFor(rows, region).cards, 0);
        EXPECT_EQ(rowFor(rows, region).totalSpecies, catalogSpeciesIn(region));
    }
}

// Cards is the sum of every Owned copy of the region's species, across species.
TEST_F(BrowseTest, CardsSumsEveryOwnedCopyOfTheRegionsSpecies) {
    const std::unordered_map<PokemonDexNum, int> counts{{kBulbasaur, 2}, {kPikachu, 3}};

    const auto rows = pokedex::regionProgress(browse.listAll(counts));

    EXPECT_EQ(rowFor(rows, Region::Kanto).capturedSpecies, 2);
    EXPECT_EQ(rowFor(rows, Region::Kanto).cards, 5);
}

// A capture counts towards its own region and no other — the guard on indexing
// the output array by the enumerator's value.
TEST_F(BrowseTest, ASpeciesCountsOnlyTowardsItsOwnRegion) {
    const std::unordered_map<PokemonDexNum, int> counts{{kChikorita, 2}};

    const auto rows = pokedex::regionProgress(browse.listAll(counts));

    EXPECT_EQ(rowFor(rows, Region::Johto).capturedSpecies, 1);
    EXPECT_EQ(rowFor(rows, Region::Johto).cards, 2);
    for (const Region region : kRegions) {
        if (region == Region::Johto) {
            continue;
        }
        EXPECT_EQ(rowFor(rows, region).capturedSpecies, 0);
        EXPECT_EQ(rowFor(rows, region).cards, 0);
    }
}

// "Captured" is the same >=1-Owned-copy predicate as the browser's Owned column,
// so the headline can never contradict the table beneath it — the claim the whole
// feature rests on, checked end to end through the repository.
TEST_F(BrowseTest, IncomingAndRemovedCopiesNeverReachTheProgress) {
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, std::nullopt));
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Incoming, std::nullopt));
    copies.add(makeCopy("c3", kBulbasaur, CardOwnership::Removed, std::nullopt));

    const auto rows = pokedex::regionProgress(browse.listAll());

    EXPECT_EQ(rowFor(rows, Region::Kanto).capturedSpecies, 1);  // Pikachu only
    EXPECT_EQ(rowFor(rows, Region::Kanto).cards, 1);
}

// The headline is a fold of the rows beneath it, not a second independent count.
TEST_F(BrowseTest, TotalProgressEqualsTheSumOfTheRegionRows) {
    const std::unordered_map<PokemonDexNum, int> counts{{kPikachu, 3}, {kChikorita, 2}};
    const auto rows = pokedex::regionProgress(browse.listAll(counts));

    const auto total = pokedex::totalProgress(rows);

    EXPECT_EQ(total.capturedSpecies, 2);
    EXPECT_EQ(total.cards, 5);
    EXPECT_EQ(total.totalSpecies, static_cast<int>(pokedex::pokemonCatalog().size()));
}

// The array's shape is a property of the function, not of its input: even given
// nothing to fold, every region reports a row (at zero).
TEST_F(BrowseTest, AnEmptySpanStillReportsEveryRegionAtZero) {
    const auto rows = pokedex::regionProgress({});

    ASSERT_EQ(rows.size(), kRegions.size());
    for (std::size_t i = 0; i < kRegions.size(); ++i) {
        EXPECT_EQ(rows[i].region, kRegions[i]);
        EXPECT_EQ(rows[i].totalSpecies, 0);
        EXPECT_EQ(rows[i].capturedSpecies, 0);
        EXPECT_EQ(rows[i].cards, 0);
    }
}

}  // namespace
