#include "core/app/binder_guide_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <set>
#include <string>
#include <vector>

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

CardBinder makeBinder(std::string id, std::vector<Region> regions) {
    CardBinder binder;
    binder.id = std::move(id);
    binder.name = "Test";
    binder.pokemonRegions = std::move(regions);
    binder.insertedAt = at("2026-07-14T09:00:00Z");
    binder.updatedAt = at("2026-07-14T09:00:00Z");
    return binder;
}

// `dex` is optional so a species-free card (Trainer/Energy) can be filed too;
// `insertedAt` pins the repository's filed ordering (inserted_at, rowid) where a
// test asserts on it.
CardCopy makeCopy(std::string id, std::optional<PokemonDexNum> dex, CardOwnership ownership,
                  std::optional<std::string> binderId,
                  const char* insertedAt = "2026-07-14T09:00:00Z") {
    CardCopy copy;
    copy.id = std::move(id);
    copy.pokemonDexNum = dex;
    copy.cardRef = CardReference{"MEW", "EN", "151/165"};
    copy.ownership = ownership;
    copy.condition = CardCondition::NearMint;
    copy.binderId = std::move(binderId);
    copy.insertedAt = at(insertedAt);
    copy.updatedAt = at(insertedAt);
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

    // The status of the FIRST row for a given dex number, or nullopt if the
    // species has no row at all. A species with several copies filed here now has
    // several rows, so prefer statusesOf() when that is what is under test.
    static std::optional<CollectionStatus> statusOf(
        const std::vector<CardBinderEntry>& entries, PokemonDexNum dex) {
        for (const CardBinderEntry& e : entries) {
            if (e.pokemon && e.pokemon->dexNumber == dex) {
                return e.status;
            }
        }
        return std::nullopt;
    }

    // Every row for a given dex number, in row order.
    static std::vector<CollectionStatus> statusesOf(
        const std::vector<CardBinderEntry>& entries, PokemonDexNum dex) {
        std::vector<CollectionStatus> statuses;
        for (const CardBinderEntry& e : entries) {
            if (e.pokemon && e.pokemon->dexNumber == dex) {
                // A row naming a species always carries a status; only a blank pocket
                // (which names no species, so never matches here) leaves it unset.
                statuses.push_back(*e.status);
            }
        }
        return statuses;
    }

    // The copy ids of every row for a given dex number, in row order. A placeholder
    // row contributes an empty string, so a stray placeholder is visible in the
    // assertion rather than silently skipped.
    static std::vector<std::string> copyIdsOf(const std::vector<CardBinderEntry>& entries,
                                              PokemonDexNum dex) {
        std::vector<std::string> ids;
        for (const CardBinderEntry& e : entries) {
            if (e.pokemon && e.pokemon->dexNumber == dex) {
                ids.push_back(e.cardCopyId.value_or(std::string{}));
            }
        }
        return ids;
    }
};

TEST_F(GuideTest, RegionBinderListsWholeRegionAllIncompleteWhenEmpty) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);

    const auto entries = guide.buildEntries(binder);

    ASSERT_EQ(entries.size(), 151u);  // Kanto is dex 1..151
    ASSERT_TRUE(entries.front().pokemon.has_value());
    ASSERT_TRUE(entries.back().pokemon.has_value());
    EXPECT_EQ(entries.front().pokemon->dexNumber, kBulbasaur);
    EXPECT_EQ(entries.back().pokemon->dexNumber, kMew);
    // Dex-ordered, every row a placeholder, every status Incomplete (no copies, no
    // wishlist).
    for (std::size_t i = 0; i < entries.size(); ++i) {
        ASSERT_TRUE(entries[i].pokemon.has_value());
        EXPECT_EQ(entries[i].pokemon->dexNumber, static_cast<int>(i) + 1);
        EXPECT_FALSE(entries[i].cardCopyId.has_value());
        EXPECT_EQ(entries[i].status, CollectionStatus::Incomplete);
    }
}

// The next five pin the row set + status of a species with exactly ONE relevant
// copy, so the row count stays 151: a lone filed copy replaces the placeholder it
// would otherwise have had, one-for-one.
TEST_F(GuideTest, IncomingCopyInBinderReadsIncoming) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Incoming, "b1"));

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(entries.size(), 151u);
    EXPECT_EQ(statusOf(entries, kPikachu), CollectionStatus::Incoming);
}

TEST_F(GuideTest, OwnedCopyInBinderReadsCompleted) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(entries.size(), 151u);
    EXPECT_EQ(statusOf(entries, kPikachu), CollectionStatus::Completed);
}

TEST_F(GuideTest, WishlistSourceReadsWished) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    addWish(kPikachu);

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Wished);
}

TEST_F(GuideTest, OwnedCopyInAnotherBinderReadsElsewhere) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    binders.add(makeBinder("b2", {}));
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b2"));

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Elsewhere);
}

// A Removed copy still carries this binder's id, so it keeps its own row here
// (frozen history is filed history) — the status no longer comes from a
// species-level fallback but from that copy itself.
TEST_F(GuideTest, RemovedCopyInBinderReadsRemoved) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Removed, "b1"));

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(entries.size(), 151u);
    EXPECT_EQ(statusOf(entries, kPikachu), CollectionStatus::Removed);
    EXPECT_EQ(copyIdsOf(entries, kPikachu), std::vector<std::string>{"c1"});
}

// Precedence: Wished outranks Elsewhere. Both are species-level verdicts, so this
// is the surviving precedence pin — the order only ever applies to a PLACEHOLDER
// row (a species with nothing filed here).
TEST_F(GuideTest, WishedOutranksElsewhere) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    binders.add(makeBinder("b2", {}));
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b2"));  // elsewhere
    addWish(kPikachu);                                                 // wished

    EXPECT_EQ(statusOf(guide.buildEntries(binder), kPikachu), CollectionStatus::Wished);
}

// A copy filed in the binder for an out-of-region species must still appear —
// filed cards are never hidden by the region filter. One copy, so one extra row.
TEST_F(GuideTest, OutOfRegionFiledCopyAppearsInRowSet) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("c1", kMisdreavus, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 152u);  // 151 Kanto + Misdreavus
    EXPECT_EQ(statusOf(entries, kMisdreavus), CollectionStatus::Completed);
}

// A multi-region binder lists the union of every scoped region's species, in dex
// order, deduplicated — Kanto (1..151) + Johto (152..251) = 251 rows.
TEST_F(GuideTest, MultiRegionBinderListsUnionOfAllRegions) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Johto});
    binders.add(binder);

    const auto entries = guide.buildEntries(binder);

    ASSERT_EQ(entries.size(), 251u);
    ASSERT_TRUE(entries.front().pokemon.has_value());
    ASSERT_TRUE(entries.back().pokemon.has_value());
    EXPECT_EQ(entries.front().pokemon->dexNumber, kBulbasaur);        // first Kanto
    EXPECT_EQ(entries.back().pokemon->dexNumber, 251);                // last Johto (Celebi)
    EXPECT_TRUE(statusOf(entries, kMew).has_value());                // 151, Kanto
    EXPECT_TRUE(statusOf(entries, kMisdreavus).has_value());         // 200, Johto
    // Dex-ordered with no gaps or repeats across the union.
    for (std::size_t i = 0; i < entries.size(); ++i) {
        ASSERT_TRUE(entries[i].pokemon.has_value());
        EXPECT_EQ(entries[i].pokemon->dexNumber, static_cast<int>(i) + 1);
    }
}

// A regionless binder has no region species, so it shows only what is filed in it.
TEST_F(GuideTest, RegionlessBinderShowsOnlyFiledSpecies) {
    const CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", kBulbasaur, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 1u);
    ASSERT_TRUE(entries[0].pokemon.has_value());
    EXPECT_EQ(entries[0].pokemon->dexNumber, kBulbasaur);
    EXPECT_EQ(entries[0].cardCopyId, "c1");  // a copy row, not a placeholder
    EXPECT_EQ(entries[0].status, CollectionStatus::Completed);
}

// A listed species holding nothing gets exactly one placeholder row — the Pokédex
// checklist the guide has always been.
TEST_F(GuideTest, SpeciesWithNothingFiledGetsOnePlaceholderRow) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    const auto bulbasaurIds = copyIdsOf(entries, kBulbasaur);
    ASSERT_EQ(bulbasaurIds.size(), 1u);
    EXPECT_EQ(bulbasaurIds[0], "");  // placeholder: no copy behind it
    EXPECT_EQ(statusOf(entries, kBulbasaur), CollectionStatus::Incomplete);
}

// A row is a slot, not a species: three physical Pikachus are three rows, adjacent
// (same dex) and in filed order, with no placeholder row left over for Pikachu.
TEST_F(GuideTest, EachFiledCopyGetsItsOwnRowAdjacentAndInFiledOrder) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    // Added in an order that DISAGREES with insertedAt (and with the ids' lexical order),
    // so the expected result can only come from the repository's documented
    // "ORDER BY inserted_at" — under a rowid or id ordering this reads {c3, c1, c2}.
    copies.add(makeCopy("c3", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T11:00:00Z"));
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T10:00:00Z"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 153u);  // 151 Kanto species, Pikachu contributing 3 rows
    EXPECT_EQ(copyIdsOf(entries, kPikachu), (std::vector<std::string>{"c1", "c2", "c3"}));

    // Adjacent: the three Pikachu rows are consecutive.
    std::size_t first = entries.size();
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].pokemon && entries[i].pokemon->dexNumber == kPikachu) {
            first = i;
            break;
        }
    }
    ASSERT_LT(first + 2, entries.size());
    for (std::size_t i = first; i < first + 3; ++i) {
        ASSERT_TRUE(entries[i].pokemon.has_value());
        EXPECT_EQ(entries[i].pokemon->dexNumber, kPikachu);
    }
}

// Each copy row carries its OWN copy's ownership — no precedence runs between the
// copies of one species (this is what replaced the old "Incoming outranks Owned in
// the same binder" fold).
TEST_F(GuideTest, CopyRowStatusFollowsThatCopysOwnership) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Incoming, "b1", "2026-07-14T10:00:00Z"));
    copies.add(makeCopy("c3", kPikachu, CardOwnership::Removed, "b1", "2026-07-14T11:00:00Z"));

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(statusesOf(entries, kPikachu),
              (std::vector<CollectionStatus>{CollectionStatus::Completed,
                                             CollectionStatus::Incoming,
                                             CollectionStatus::Removed}));
}

// Species-free cards (Trainer/Energy/promo) are filed in binders like any other
// card, and land after every species row — they carry no dex number to sort among
// them.
TEST_F(GuideTest, SpeciesFreeCopiesAppearLastInFiledOrder) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    // Added newest-first, so the asserted order can only come from insertedAt.
    copies.add(
        makeCopy("t2", std::nullopt, CardOwnership::Owned, "b1", "2026-07-14T10:00:00Z"));
    copies.add(
        makeCopy("t1", std::nullopt, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 153u);  // 151 Kanto placeholders + 2 species-free cards
    // Every species row comes first.
    for (std::size_t i = 0; i < 151; ++i) {
        EXPECT_TRUE(entries[i].pokemon.has_value());
    }
    EXPECT_FALSE(entries[151].pokemon.has_value());
    EXPECT_FALSE(entries[152].pokemon.has_value());
    EXPECT_EQ(entries[151].cardCopyId, "t1");
    EXPECT_EQ(entries[152].cardCopyId, "t2");
    EXPECT_EQ(entries[151].status, CollectionStatus::Completed);
}

// The sharp version of the rule: species-free cards come after every SPECIES COPY row,
// not merely after the placeholders. An implementation that emitted rows in plain filed
// order (species-free ones inline, as listByBinder hands them over) would put the Trainer
// card first here — which no other test would catch, since none files both kinds.
TEST_F(GuideTest, SpeciesFreeCopiesFollowSpeciesCopyRowsEvenWhenFiledFirst) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(
        makeCopy("t1", std::nullopt, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T10:00:00Z"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 152u);  // 151 Kanto species (Pikachu's row is its copy) + 1 card
    EXPECT_EQ(copyIdsOf(entries, kPikachu), std::vector<std::string>{"c1"});
    // The Trainer is last despite being filed FIRST.
    EXPECT_FALSE(entries.back().pokemon.has_value());
    EXPECT_EQ(entries.back().cardCopyId, "t1");
}

// Nothing filed here is invisible — a species-free card has no placeholder row to
// fall back on, so if it were dropped it would vanish from the guide entirely.
TEST_F(GuideTest, RemovedSpeciesFreeCopyStillGetsARow) {
    const CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("t1", std::nullopt, CardOwnership::Removed, "b1"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_FALSE(entries[0].pokemon.has_value());
    EXPECT_EQ(entries[0].cardCopyId, "t1");
    EXPECT_EQ(entries[0].status, CollectionStatus::Removed);
}

// A wish is a species-level want; once a card is actually filed here the row
// speaks for that card, so the Wished placeholder is gone.
TEST_F(GuideTest, WishedSpeciesWithACopyFiledHereShowsTheCopyNotTheWish) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    addWish(kPikachu);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(statusesOf(entries, kPikachu),
              std::vector<CollectionStatus>{CollectionStatus::Completed});
    EXPECT_EQ(copyIdsOf(entries, kPikachu), std::vector<std::string>{"c1"});
}

// CONTRACT, not a regression: a species with any copy filed here shows only its
// copy rows, so the "you own one in another binder" (Elsewhere) hint no longer
// surfaces for it — including when the copy filed here is Removed. That signal was
// a property of the old one-row-per-species fold; a per-copy row set has no place
// to put it.
TEST_F(GuideTest, SpeciesOwnedElsewhereWithACopyFiledHereShowsOnlyTheCopyRow) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    binders.add(makeBinder("b2", {}));
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b2"));    // elsewhere
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Removed, "b1"));  // filed here

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(statusesOf(entries, kPikachu),
              std::vector<CollectionStatus>{CollectionStatus::Removed});
    EXPECT_EQ(copyIdsOf(entries, kPikachu), std::vector<std::string>{"c2"});
}

// CONTRACT, not a regression — the sharpest instance of the rule above, and the one most
// likely to be reported as a bug: a species you still WANT, whose only copy filed here was
// removed, shows just that Removed copy row. The Wished placeholder is gone because the
// species does have something filed here; the want itself is still recorded, and the
// wishlist surfaces it.
TEST_F(GuideTest, WishedSpeciesWithOnlyARemovedCopyFiledHereShowsOnlyTheRemovedRow) {
    const CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    addWish(kPikachu);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Removed, "b1"));

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(statusesOf(entries, kPikachu),
              std::vector<CollectionStatus>{CollectionStatus::Removed});
    EXPECT_EQ(copyIdsOf(entries, kPikachu), std::vector<std::string>{"c1"});
}

// A dex number with no species in the catalog (a hand-edited row) has no species
// row to join, so the copy falls to the species-free tail rather than being
// dropped — again, nothing filed here is invisible.
TEST_F(GuideTest, CopyWithUnresolvableDexNumberFallsToTheTail) {
    const CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", 99999, CardOwnership::Owned, "b1"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_FALSE(entries[0].pokemon.has_value());
    EXPECT_EQ(entries[0].cardCopyId, "c1");
}

// A regionless binder holding only Trainer/Energy cards — the "misc binder" case —
// lists them rather than reading as empty.
TEST_F(GuideTest, RegionlessBinderWithOnlySpeciesFreeCopiesListsThem) {
    const CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(
        makeCopy("t1", std::nullopt, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(
        makeCopy("t2", std::nullopt, CardOwnership::Incoming, "b1", "2026-07-14T10:00:00Z"));

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].cardCopyId, "t1");
    EXPECT_EQ(entries[0].status, CollectionStatus::Completed);
    EXPECT_EQ(entries[1].cardCopyId, "t2");
    EXPECT_EQ(entries[1].status, CollectionStatus::Incoming);
}

// --- blank pockets ----------------------------------------------------------------
//
// Blanks live on the binder value, so these drive them directly rather than through
// storage — the guide is a pure function of (binder, copies, wishlist).

namespace {

constexpr int kChespin = 650;  // Kalos — the first species after Kanto's 151
constexpr int kQuilladin = 651;

bool isBlank(const CardBinderEntry& e) {
    return !e.pokemon && !e.cardCopyId && !e.status;
}

int blankCount(const std::vector<CardBinderEntry>& entries) {
    int n = 0;
    for (const CardBinderEntry& e : entries) {
        n += isBlank(e) ? 1 : 0;
    }
    return n;
}

// The row index of the first row naming `dex`, or -1.
int indexOfSpecies(const std::vector<CardBinderEntry>& entries, PokemonDexNum dex) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].pokemon && entries[i].pokemon->dexNumber == dex) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

}  // namespace

TEST_F(GuideTest, BlankBeforeASpeciesEmitsAnEmptyRowAheadOfIt) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    binder.pocketBlanks = {{.beforeDexNum = kPikachu, .blanks = 1}};

    const auto entries = guide.buildEntries(binder);
    const int pikachuRow = indexOfSpecies(entries, kPikachu);
    ASSERT_GT(pikachuRow, 0);
    EXPECT_TRUE(isBlank(entries[static_cast<std::size_t>(pikachuRow) - 1]));
    EXPECT_EQ(blankCount(entries), 1);
}

// A blank row stands for an empty pocket: it names no species, no card, and — unlike
// every other row — reports no CollectionStatus at all.
TEST_F(GuideTest, BlankRowCarriesNoSpeciesNoCopyAndNoStatus) {
    CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));
    binder.pocketBlanks = {{.beforeDexNum = kPikachu, .blanks = 1}};

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_FALSE(entries[0].pokemon.has_value());
    EXPECT_FALSE(entries[0].cardCopyId.has_value());
    EXPECT_FALSE(entries[0].status.has_value());
}

TEST_F(GuideTest, SeveralBlanksAtOneAnchorEmitThatManyRows) {
    CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));
    binder.pocketBlanks = {{.beforeDexNum = kPikachu, .blanks = 3}};

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 4u);
    EXPECT_EQ(blankCount(entries), 3);
    EXPECT_EQ(entries[3].cardCopyId, "c1");
}

// The page break has to be settable ahead of a species you don't own yet — that is
// exactly how you reserve the next page for a region before collecting it.
TEST_F(GuideTest, BlankBeforeAPlaceholderRowStillEmits) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    binder.pocketBlanks = {{.beforeDexNum = kMew, .blanks = 2}};

    const auto entries = guide.buildEntries(binder);
    const int mewRow = indexOfSpecies(entries, kMew);
    ASSERT_GT(mewRow, 1);
    EXPECT_TRUE(isBlank(entries[static_cast<std::size_t>(mewRow) - 1]));
    EXPECT_TRUE(isBlank(entries[static_cast<std::size_t>(mewRow) - 2]));
    // The placeholder itself is untouched.
    EXPECT_FALSE(entries[static_cast<std::size_t>(mewRow)].cardCopyId.has_value());
}

// A species anchor names the species, so its blanks precede the whole block of copies
// rather than landing between two of them.
TEST_F(GuideTest, BlankAnchoredToASpeciesPrecedesEveryCopyRowOfIt) {
    CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(makeCopy("c2", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T10:00:00Z"));
    binder.pocketBlanks = {{.beforeDexNum = kPikachu, .blanks = 1}};

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_TRUE(isBlank(entries[0]));
    EXPECT_EQ(entries[1].cardCopyId, "c1");
    EXPECT_EQ(entries[2].cardCopyId, "c2");
}

// A species-free card has no dex number to name it, so its blanks anchor to the copy.
TEST_F(GuideTest, BlankAnchoredToASpeciesFreeCardPrecedesThatCardsRow) {
    CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(makeCopy("t1", std::nullopt, CardOwnership::Owned, "b1", "2026-07-14T10:00:00Z"));
    binder.pocketBlanks = {{.beforeCopyId = "t1", .blanks = 1}};

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].cardCopyId, "c1");
    EXPECT_TRUE(isBlank(entries[1]));
    EXPECT_EQ(entries[2].cardCopyId, "t1");
}

// An orphan: the anchor species isn't listed here and holds no card, so there is no row
// to sit before. The blank produces nothing — and, crucially, is not pruned, so
// re-scoping the region brings the layout back.
TEST_F(GuideTest, BlankWithAnUnlistedAnchorSpeciesEmitsNothing) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    binder.pocketBlanks = {{.beforeDexNum = kChespin, .blanks = 2}};

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(blankCount(entries), 0);
    EXPECT_EQ(indexOfSpecies(entries, kChespin), -1);
}

TEST_F(GuideTest, BlankAnchoredToAMissingCopyEmitsNothing) {
    CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));
    binder.pocketBlanks = {{.beforeCopyId = "deleted-copy", .blanks = 2}};

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(blankCount(entries), 0);
}

// A blank naming both anchors or neither, or counting no pockets, can't be placed —
// dropped without disturbing the rest of the guide.
TEST_F(GuideTest, UnplaceableBlanksAreIgnored) {
    CardBinder binder = makeBinder("b1", {});
    binders.add(binder);
    copies.add(makeCopy("c1", kPikachu, CardOwnership::Owned, "b1"));
    binder.pocketBlanks = {
        {.beforeDexNum = kPikachu, .beforeCopyId = "c1", .blanks = 1},  // both
        {.blanks = 1},                                                  // neither
        {.beforeDexNum = kPikachu, .blanks = 0},                        // no pockets
    };

    const auto entries = guide.buildEntries(binder);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].cardCopyId, "c1");
}

TEST_F(GuideTest, BlanksDoNotChangeTheRelativeOrderOfTheRealRows) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    const auto withoutBlanks = guide.buildEntries(binder);

    binder.pocketBlanks = {{.beforeDexNum = kPikachu, .blanks = 2},
                           {.beforeDexNum = kMew, .blanks = 1}};
    const auto withBlanks = guide.buildEntries(binder);

    std::vector<PokemonDexNum> before;
    for (const CardBinderEntry& e : withoutBlanks) {
        before.push_back(e.pokemon ? e.pokemon->dexNumber : 0);
    }
    std::vector<PokemonDexNum> after;
    for (const CardBinderEntry& e : withBlanks) {
        if (!isBlank(e)) {
            after.push_back(e.pokemon ? e.pokemon->dexNumber : 0);
        }
    }
    EXPECT_EQ(before, after);
    EXPECT_EQ(withBlanks.size(), withoutBlanks.size() + 3);
}

// The motivating scenario, end to end. A Kanto+Kalos binder runs 1..151 then 650..;
// with 9 pockets to a page Kanto ends 7 pockets into page 17, so Kalos starts mid-page
// and Chespin's evolution line splits across two pages. Two blanks before Chespin push
// it to the first pocket of the next page.
TEST_F(GuideTest, TheKantoToKalosPageBreakScenario) {
    CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Kalos});
    binders.add(binder);

    const auto before = guide.buildEntries(binder);
    const int chespinBefore = indexOfSpecies(before, kChespin);
    ASSERT_EQ(chespinBefore, 151);       // 0-based: right after Kanto's 151 species
    EXPECT_EQ(chespinBefore % 9, 7);     // the 8th pocket of its page — mid-page

    binder.pocketBlanks = {{.beforeDexNum = kChespin, .blanks = 2}};
    const auto after = guide.buildEntries(binder);
    const int chespinAfter = indexOfSpecies(after, kChespin);
    EXPECT_EQ(chespinAfter, chespinBefore + 2);  // shifted by exactly the two blanks
    EXPECT_EQ(chespinAfter % 9, 0);              // now the FIRST pocket of a page
    // And its evolution line follows it onto that same page.
    EXPECT_EQ(indexOfSpecies(after, kQuilladin), chespinAfter + 1);
}

// --- moved cards (placements) --------------------------------------------------------
//
// Like blanks, placements live on the binder value, so these drive them directly.

namespace {

// The row index of the row standing for `copyId`, or -1.
int indexOfCopy(const std::vector<CardBinderEntry>& entries, const std::string& copyId) {
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].cardCopyId == copyId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// How many rows name `copyId` — every filed card must have exactly one, whatever the
// arrangement says.
int rowsForCopy(const std::vector<CardBinderEntry>& entries, const std::string& copyId) {
    int n = 0;
    for (const CardBinderEntry& e : entries) {
        n += e.cardCopyId == copyId ? 1 : 0;
    }
    return n;
}

}  // namespace

// The base case: a species-free card, which naturally lands after every species row,
// pinned before a species instead.
TEST_F(GuideTest, PlacedCardMovesToItsAnchorAndLeavesItsNaturalSpot) {
    CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Kalos});
    binders.add(binder);
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    const auto before = guide.buildEntries(binder);
    EXPECT_EQ(indexOfCopy(before, "trainer"), static_cast<int>(before.size()) - 1);

    binder.cardPlacements = {{.cardCopyId = "trainer", .beforeDexNum = kChespin}};
    const auto after = guide.buildEntries(binder);
    EXPECT_EQ(indexOfCopy(after, "trainer"), indexOfSpecies(after, kChespin) - 1);
    EXPECT_EQ(rowsForCopy(after, "trainer"), 1);  // moved, not duplicated
    EXPECT_EQ(after.size(), before.size());       // one slot vacated, one taken
}

// A card pinned before ANOTHER CARD, which is the usual anchor — a move targets one
// exact sleeve, so it must be able to land between two copies of the same species.
TEST_F(GuideTest, PlacedCardCanSitBetweenTwoCopiesOfOneSpecies) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("pika-a", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(makeCopy("pika-b", kPikachu, CardOwnership::Owned, "b1", "2026-07-14T10:00:00Z"));
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    binder.cardPlacements = {{.cardCopyId = "trainer", .beforeCopyId = "pika-b"}};
    const auto entries = guide.buildEntries(binder);

    EXPECT_EQ(indexOfCopy(entries, "trainer"), indexOfCopy(entries, "pika-a") + 1);
    EXPECT_EQ(indexOfCopy(entries, "pika-b"), indexOfCopy(entries, "trainer") + 1);
}

// The "at the very end" anchor — neither half set. Without it the last pocket would be
// unreachable, since every other target is phrased as "before some row".
TEST_F(GuideTest, PlacementWithNoAnchorLandsAfterEveryRow) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("pika", kPikachu, CardOwnership::Owned, "b1"));

    binder.cardPlacements = {{.cardCopyId = "pika"}};
    const auto entries = guide.buildEntries(binder);

    EXPECT_EQ(indexOfCopy(entries, "pika"), static_cast<int>(entries.size()) - 1);
}

// THE ORDERING RULE, and the reason every target pocket is expressible: at one anchor,
// moved cards come first (each preceded by its own riders), then that anchor's blanks,
// then the natural row. So a move re-anchors the blanks that must PRECEDE the card onto
// the card itself and leaves the rest on the anchor.
TEST_F(GuideTest, MovedCardsPrecedeTheAnchorsOwnBlanks) {
    CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Kalos});
    binders.add(binder);
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    // One blank riding on the moved card, one left on Chespin: [blank][trainer][blank].
    binder.pocketBlanks = {{.beforeCopyId = "trainer", .blanks = 1},
                           {.beforeDexNum = kChespin, .blanks = 1}};
    binder.cardPlacements = {{.cardCopyId = "trainer", .beforeDexNum = kChespin}};

    const auto entries = guide.buildEntries(binder);
    const int chespin = indexOfSpecies(entries, kChespin);
    ASSERT_GT(chespin, 2);
    EXPECT_TRUE(isBlank(entries[chespin - 1]));                    // stayed on Chespin
    EXPECT_EQ(entries[chespin - 2].cardCopyId, "trainer");         // the moved card
    EXPECT_TRUE(isBlank(entries[chespin - 3]));                    // rode on the card
}

// The screenshot case, end to end: Kanto ends mid-page 17 with two blanks before Chespin,
// and a Trainer card is moved into the SECOND of them (17·3×3). One blank is consumed and
// the other re-anchored onto the card, so the card takes that exact pocket and Chespin
// still opens page 18 at 1×1.
TEST_F(GuideTest, MovingACardIntoTheSecondBlankOfAGapTakesThatExactPocket) {
    CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Kalos});
    binders.add(binder);
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    binder.pocketBlanks = {{.beforeDexNum = kChespin, .blanks = 2}};
    const auto gapped = guide.buildEntries(binder);
    const int chespinGapped = indexOfSpecies(gapped, kChespin);
    ASSERT_EQ(chespinGapped % 9, 0);  // Chespin opens a page, as the blanks feature left it

    // The move: consume one blank, re-anchor the surviving one onto the card.
    binder.pocketBlanks = {{.beforeCopyId = "trainer", .blanks = 1}};
    binder.cardPlacements = {{.cardCopyId = "trainer", .beforeDexNum = kChespin}};

    const auto moved = guide.buildEntries(binder);
    const int chespin = indexOfSpecies(moved, kChespin);
    EXPECT_EQ(chespin, chespinGapped);                     // page 18 · 1×1, unmoved
    EXPECT_EQ(indexOfCopy(moved, "trainer"), chespin - 1);  // page 17 · 3×3
    EXPECT_TRUE(isBlank(moved[chespin - 2]));              // page 17 · 3×2, still empty
    EXPECT_EQ(blankCount(moved), 1);                       // one blank was filled
}

// Placements CHAIN: a card may be pinned before a card that is itself placed, which is
// how you target a pocket a moved card already holds.
TEST_F(GuideTest, PlacementsChainThroughAnotherMovedCard) {
    CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Kalos});
    binders.add(binder);
    copies.add(makeCopy("first", std::nullopt, CardOwnership::Owned, "b1"));
    copies.add(makeCopy("second", std::nullopt, CardOwnership::Owned, "b1"));

    binder.cardPlacements = {{.cardCopyId = "first", .beforeDexNum = kChespin},
                             {.cardCopyId = "second", .beforeCopyId = "first"}};

    const auto entries = guide.buildEntries(binder);
    const int chespin = indexOfSpecies(entries, kChespin);
    EXPECT_EQ(indexOfCopy(entries, "first"), chespin - 1);
    EXPECT_EQ(indexOfCopy(entries, "second"), chespin - 2);
}

// Two cards sharing one anchor are ordered by `ordinal`, ascending and NEAREST-LAST — so
// appending (max + 1) puts the newest arrival closest to the anchor row, which is where a
// card aimed at that row's own pocket belongs.
TEST_F(GuideTest, PlacementsSharingAnAnchorRunInOrdinalOrder) {
    CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Kalos});
    binders.add(binder);
    copies.add(makeCopy("early", std::nullopt, CardOwnership::Owned, "b1"));
    copies.add(makeCopy("late", std::nullopt, CardOwnership::Owned, "b1"));

    binder.cardPlacements = {{.cardCopyId = "late", .beforeDexNum = kChespin, .ordinal = 1},
                             {.cardCopyId = "early", .beforeDexNum = kChespin, .ordinal = 0}};

    const auto entries = guide.buildEntries(binder);
    const int chespin = indexOfSpecies(entries, kChespin);
    EXPECT_EQ(indexOfCopy(entries, "early"), chespin - 2);
    EXPECT_EQ(indexOfCopy(entries, "late"), chespin - 1);
}

// An ORPHANED placement — its anchor species isn't listed by this binder — is ignored and
// the card renders in its natural position. It is never dropped: this guide's contract is
// that nothing filed here is invisible, so no arrangement may cost the user a card.
TEST_F(GuideTest, OrphanedPlacementLeavesTheCardInItsNaturalPosition) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});  // no Kalos, so no Chespin row
    binders.add(binder);
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    binder.cardPlacements = {{.cardCopyId = "trainer", .beforeDexNum = kChespin}};
    const auto entries = guide.buildEntries(binder);

    EXPECT_EQ(rowsForCopy(entries, "trainer"), 1);
    EXPECT_EQ(indexOfCopy(entries, "trainer"), static_cast<int>(entries.size()) - 1);
}

// Same for an anchor card that isn't filed in this binder any more.
TEST_F(GuideTest, PlacementAnchoredToAMissingCardIsIgnored) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    binder.cardPlacements = {{.cardCopyId = "trainer", .beforeCopyId = "gone"}};
    const auto entries = guide.buildEntries(binder);

    EXPECT_EQ(rowsForCopy(entries, "trainer"), 1);
    EXPECT_EQ(indexOfCopy(entries, "trainer"), static_cast<int>(entries.size()) - 1);
}

// A CYCLE never terminates at a real row, so the fixed point honours neither placement
// and both cards fall back to natural order — rather than recursing forever or vanishing.
TEST_F(GuideTest, CircularPlacementsFallBackToNaturalOrder) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("a", std::nullopt, CardOwnership::Owned, "b1", "2026-07-14T09:00:00Z"));
    copies.add(makeCopy("b", std::nullopt, CardOwnership::Owned, "b1", "2026-07-14T10:00:00Z"));

    binder.cardPlacements = {{.cardCopyId = "a", .beforeCopyId = "b"},
                             {.cardCopyId = "b", .beforeCopyId = "a"}};

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(rowsForCopy(entries, "a"), 1);
    EXPECT_EQ(rowsForCopy(entries, "b"), 1);
    EXPECT_LT(indexOfCopy(entries, "a"), indexOfCopy(entries, "b"));  // filed order
}

// A placement naming both anchor halves points at no single row, and one naming a card
// filed in another binder isn't ours to honour. Both are dropped like a malformed blank.
TEST_F(GuideTest, MalformedAndForeignPlacementsAreIgnored) {
    CardBinder binder = makeBinder("b1", {Region::Kanto});
    binders.add(binder);
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));
    copies.add(makeCopy("elsewhere", kMew, CardOwnership::Owned, std::nullopt));

    binder.cardPlacements = {
        {.cardCopyId = "trainer", .beforeDexNum = kPikachu, .beforeCopyId = "x"},
        {.cardCopyId = "elsewhere", .beforeDexNum = kPikachu},
    };

    const auto entries = guide.buildEntries(binder);
    EXPECT_EQ(indexOfCopy(entries, "trainer"), static_cast<int>(entries.size()) - 1);
    EXPECT_EQ(rowsForCopy(entries, "elsewhere"), 0);  // not filed here at all
}

// When a species' ONLY copy is moved away, its natural spot emits nothing — not a
// placeholder. The card is still in the binder and still on the checklist, just on
// another page; a placeholder would claim a pocket that no longer exists and paginate
// everything after it wrongly.
TEST_F(GuideTest, SpeciesWhoseOnlyCopyMovedAwayLeavesNoPlaceholderBehind) {
    CardBinder binder = makeBinder("b1", {});  // region-less: only filed species are listed
    binders.add(binder);
    copies.add(makeCopy("pika", kPikachu, CardOwnership::Owned, "b1"));
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    binder.cardPlacements = {{.cardCopyId = "pika", .beforeCopyId = "trainer"}};
    const auto entries = guide.buildEntries(binder);

    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].cardCopyId, "pika");
    EXPECT_EQ(entries[1].cardCopyId, "trainer");
    EXPECT_EQ(copyIdsOf(entries, kPikachu), (std::vector<std::string>{"pika"}));
}

// A blank pinned to a card travels with it — a copy anchor means "immediately before this
// row" wherever the row sits, which is exactly what lets a move carry its gap along.
TEST_F(GuideTest, BlankAnchoredToAMovedCardFollowsIt) {
    CardBinder binder = makeBinder("b1", {Region::Kanto, Region::Kalos});
    binders.add(binder);
    copies.add(makeCopy("trainer", std::nullopt, CardOwnership::Owned, "b1"));

    binder.pocketBlanks = {{.beforeCopyId = "trainer", .blanks = 1}};
    binder.cardPlacements = {{.cardCopyId = "trainer", .beforeDexNum = kChespin}};

    const auto entries = guide.buildEntries(binder);
    const int trainer = indexOfCopy(entries, "trainer");
    ASSERT_GT(trainer, 0);
    EXPECT_TRUE(isBlank(entries[trainer - 1]));
    EXPECT_EQ(blankCount(entries), 1);
}

}  // namespace
