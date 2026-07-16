#include "core/app/binder_guide_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>

#include "core/domain/card_binder.h"
#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/domain/collection_status.h"
#include "core/domain/region.h"
#include "core/domain/wishlist.h"
#include "core/storage/card_binder_repository.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/wishlist_repository.h"

namespace {

using pokedex::CardBinder;
using pokedex::CardBinderEntry;
using pokedex::CardBinderRepository;
using pokedex::CardCondition;
using pokedex::CardCopy;
using pokedex::CardCopyRepository;
using pokedex::CardOwnership;
using pokedex::CardReference;
using pokedex::CollectionStatus;
using pokedex::Database;
using pokedex::PokemonDexNum;
using pokedex::Region;
using pokedex::Timestamp;
using pokedex::Wishlist;
using pokedex::WishlistRepository;

// Kanto reference dex numbers used across these tests.
constexpr int kBulbasaur = 1;
constexpr int kPikachu = 25;
constexpr int kMew = 151;
constexpr int kMisdreavus = 200;  // Johto — used to test out-of-region filed copies

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

CardBinder makeBinder(std::string id, std::optional<Region> region) {
    CardBinder binder;
    binder.id = std::move(id);
    binder.name = "Test";
    binder.pokemonRegion = region;
    binder.insertedAt = at("2026-07-14T09:00:00Z");
    binder.updatedAt = at("2026-07-14T09:00:00Z");
    return binder;
}

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

// Fixture wiring the migrated DB + the three repositories + the service.
struct GuideTest : ::testing::Test {
    Database db{":memory:"};
    CardBinderRepository binders{db};
    CardCopyRepository copies{db};
    WishlistRepository wishlist{db};
    pokedex::BinderGuideService guide{copies, wishlist};

    GuideTest() { db.migrate(); }

    void addWish(PokemonDexNum dex) {
        Wishlist w;
        w.pokemonDexNum = dex;
        w.sources = {"ebay"};
        w.insertedAt = at("2026-07-14T09:00:00Z");
        w.updatedAt = at("2026-07-14T09:00:00Z");
        wishlist.save(w);
    }

    // The status of a given dex number in the entries, or nullopt if absent.
    static std::optional<CollectionStatus> statusOf(
        const std::vector<CardBinderEntry>& entries, PokemonDexNum dex) {
        for (const CardBinderEntry& e : entries) {
            if (e.pokemon.dexNumber == dex) {
                return e.status;
            }
        }
        return std::nullopt;
    }
};

TEST_F(GuideTest, RegionBinderListsWholeRegionAllIncompleteWhenEmpty) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);

    const auto entries = guide.buildEntries(binder);

    ASSERT_EQ(entries.size(), 151u);  // Kanto is dex 1..151
    EXPECT_EQ(entries.front().pokemon.dexNumber, kBulbasaur);
    EXPECT_EQ(entries.back().pokemon.dexNumber, kMew);
    // Dex-ordered and every status Incomplete (no copies, no wishlist).
    for (std::size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ(entries[i].pokemon.dexNumber, static_cast<int>(i) + 1);
        EXPECT_EQ(entries[i].status, CollectionStatus::Incomplete);
    }
}

TEST_F(GuideTest, IncomingCopyInBinderReadsIncoming) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Incoming, "b1"));

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Incoming);
}

TEST_F(GuideTest, OwnedCopyInBinderReadsCompleted) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Completed);
}

TEST_F(GuideTest, WishlistSourceReadsWished) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    addWish(kPikachu);

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Wished);
}

TEST_F(GuideTest, OwnedCopyInAnotherBinderReadsElsewhere) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    binders.add(makeBinder("b2", std::nullopt));
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b2"));

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Elsewhere);
}

TEST_F(GuideTest, RemovedCopyInBinderReadsRemoved) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Removed, "b1"));

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Removed);
}

// Precedence: an arriving card outranks one already owned in the same binder.
TEST_F(GuideTest, IncomingOutranksOwnedInSameBinder) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Incoming, "b1"));

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Incoming);
}

// Precedence: Wished outranks Elsewhere.
TEST_F(GuideTest, WishedOutranksElsewhere) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    binders.add(makeBinder("b2", std::nullopt));
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b2"));  // elsewhere
    addWish(kPikachu);                                                 // wished

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Wished);
}

// A copy filed in the binder for an out-of-region species must still appear —
// filed cards are never hidden by the region filter.
TEST_F(GuideTest, OutOfRegionFiledCopyAppearsInRowSet) {
    const CardBinder binder = makeBinder("b1", Region::Kanto);
    binders.add(binder);
    copies.add(makeCopy("c1", kMisdreavus, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 152u);  // 151 Kanto + Misdreavus
    EXPECT_EQ(statusOf(entries, kMisdreavus), CollectionStatus::Completed);
}

// A regionless binder has no region species, so it shows only what is filed in it.
TEST_F(GuideTest, RegionlessBinderShowsOnlyFiledSpecies) {
    const CardBinder binder = makeBinder("b1", std::nullopt);
    binders.add(binder);
    copies.add(makeCopy("c1", kBulbasaur, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].pokemon.dexNumber, kBulbasaur);
    EXPECT_EQ(entries[0].status, CollectionStatus::Completed);
}

}  // namespace
