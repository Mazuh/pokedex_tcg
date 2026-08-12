#include "core/app/binder_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/region.h"
#include "core/storage/card_binder_repository.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace {

using pokedex::BinderError;
using pokedex::BinderService;
using pokedex::CardBinder;
using pokedex::CardBinderBlank;
using pokedex::CardBinderPocketGrid;
using pokedex::CardBinderRepository;
using pokedex::Database;
using pokedex::Region;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

// A test rig with an in-memory DB, a clock the test drives, and deterministic
// sequential ids (so assertions don't depend on random UUIDs).
struct Fixture {
    Database db{":memory:"};
    CardBinderRepository repo{db};
    Timestamp now = at("2026-07-14T09:00:00Z");
    int idCounter = 0;
    BinderService service{repo, [this] { return now; },
                          [this] { return "id-" + std::to_string(++idCounter); }};

    Fixture() { db.migrate(); }
};

TEST(BinderServiceTest, CreateStampsIdRegionsAndTimes) {
    Fixture f;
    const CardBinder created =
        f.service.create("Johto Album", {Region::Kanto, Region::Johto});

    EXPECT_EQ(created.id, "id-1");
    EXPECT_EQ(created.name, "Johto Album");
    ASSERT_EQ(created.pokemonRegions.size(), 2u);
    EXPECT_EQ(created.insertedAt, f.now);
    EXPECT_EQ(created.updatedAt, f.now);

    // And it (with both regions) was persisted.
    const auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].id, "id-1");
    EXPECT_EQ(binders[0].pokemonRegions,
              (std::vector<Region>{Region::Kanto, Region::Johto}));
}

TEST(BinderServiceTest, CreateWithoutRegionsLeavesThemEmpty) {
    Fixture f;
    const CardBinder created = f.service.create("Loose Cards", {});
    EXPECT_TRUE(created.pokemonRegions.empty());
}

TEST(BinderServiceTest, EachCreateGetsADistinctId) {
    Fixture f;
    const CardBinder a = f.service.create("A", {});
    const CardBinder b = f.service.create("B", {});
    EXPECT_NE(a.id, b.id);
}

TEST(BinderServiceTest, CreateTrimsNameAndRejectsBlank) {
    Fixture f;
    EXPECT_EQ(f.service.create("  Spaced  ", {}).name, "Spaced");
    EXPECT_THROW(f.service.create("   ", {}), BinderError);
    EXPECT_THROW(f.service.create("", {}), BinderError);
}

TEST(BinderServiceTest, UpdateChangesNameAndBumpsUpdatedAtOnly) {
    Fixture f;
    const CardBinder created = f.service.create("Old", {Region::Kanto});

    f.now = at("2026-07-20T15:00:00Z");
    f.service.update(created.id, "New", {Region::Kanto});

    const auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].name, "New");
    EXPECT_EQ(binders[0].updatedAt, at("2026-07-20T15:00:00Z"));
    EXPECT_EQ(binders[0].insertedAt, at("2026-07-14T09:00:00Z"));
    EXPECT_EQ(binders[0].pokemonRegions, (std::vector<Region>{Region::Kanto}));
}

// A binder's regions are settled at create() and update() has no way to touch them.
// They decide which species get a reserved slot and hence where every page break falls,
// so re-scoping an album that already holds cards has no sound answer — where would a
// newly added second region begin, with 200 cards filed against the first? — and would
// orphan the blanks and moves arranged against the old layout.
TEST(BinderServiceTest, UpdateCanRescopeAnEmptyBinderButNotAFilledOne) {
    Fixture f;
    const CardBinder created = f.service.create("Old", {Region::Kanto});
    EXPECT_TRUE(f.service.canChangeRegions(created.id));

    // Empty: a scope typed wrong at creation is still correctable.
    const CardBinder rescoped = f.service.update(created.id, "New", {Region::Johto});
    EXPECT_EQ(rescoped.pokemonRegions, (std::vector<Region>{Region::Johto}));

    // Arrange something against that layout and the regions settle.
    f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 200, .blanks = 1});
    EXPECT_FALSE(f.service.canChangeRegions(created.id));
    EXPECT_THROW(f.service.update(created.id, "New", {Region::Hoenn}), std::exception);

    // But an ordinary rename, carrying the same regions through, still goes in.
    const CardBinder renamed = f.service.update(created.id, "Renamed", {Region::Johto});
    EXPECT_EQ(renamed.name, "Renamed");
    EXPECT_EQ(renamed.pokemonRegions, (std::vector<Region>{Region::Johto}));
}

TEST(BinderServiceTest, UpdateRejectsBlank) {
    Fixture f;
    const CardBinder created = f.service.create("Keep", {});
    EXPECT_THROW(f.service.update(created.id, "  ", {}), BinderError);
}

TEST(BinderServiceTest, UpdateMissingBinderThrows) {
    Fixture f;
    EXPECT_THROW(f.service.update("does-not-exist", "New", {}),
                 pokedex::StorageError);
}

TEST(BinderServiceTest, RemoveDropsTheBinderFromTheList) {
    Fixture f;
    const CardBinder a = f.service.create("A", {});
    f.service.create("B", {});

    f.service.remove(a.id);

    const auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].name, "B");
}

TEST(BinderServiceTest, DefaultServiceMintsNonEmptyUniqueIds) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    BinderService service(repo);  // real UUID generator + system clock

    const CardBinder a = service.create("A", {});
    const CardBinder b = service.create("B", {});
    EXPECT_FALSE(a.id.empty());
    EXPECT_NE(a.id, b.id);
}

// --- physical layout -------------------------------------------------------------

TEST(BinderServiceTest, CreateRecordsCapacityAndPocketGrid) {
    Fixture f;
    const CardBinder created =
        f.service.create("Kanto Journey", {Region::Kanto}, 360,
                         CardBinderPocketGrid{.rows = 3, .columns = 3});

    EXPECT_EQ(created.capacity, 360);
    ASSERT_TRUE(created.pocketGrid.has_value());
    EXPECT_EQ(pocketsPerPage(*created.pocketGrid), 9);

    const auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].capacity, 360);
    ASSERT_TRUE(binders[0].pocketGrid.has_value());
    EXPECT_EQ(binders[0].pocketGrid->columns, 3);
}

// The layout is optional, but a recorded one has to describe a real album.
TEST(BinderServiceTest, CreateRejectsANonPositiveCapacityOrGridSide) {
    Fixture f;
    EXPECT_THROW(f.service.create("Bad", {}, 0), BinderError);
    EXPECT_THROW(f.service.create("Bad", {}, -5), BinderError);
    EXPECT_THROW(f.service.create("Bad", {}, std::nullopt,
                                  CardBinderPocketGrid{.rows = 3, .columns = 0}),
                 BinderError);
    EXPECT_THROW(f.service.create("Bad", {}, std::nullopt,
                                  CardBinderPocketGrid{.rows = 0, .columns = 3}),
                 BinderError);
    EXPECT_TRUE(f.service.list().empty());  // nothing was persisted
}

TEST(BinderServiceTest, UpdateChangesTheLayoutAndCanClearIt) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto Journey", {});

    f.now = at("2026-07-15T12:00:00Z");
    const CardBinder measured =
        f.service.update(created.id, "Kanto Journey", {}, 360,
                         CardBinderPocketGrid{.rows = 3, .columns = 3});
    EXPECT_EQ(measured.capacity, 360);
    EXPECT_EQ(measured.updatedAt, f.now);

    const CardBinder cleared = f.service.update(created.id, "Kanto Journey", {});
    EXPECT_FALSE(cleared.capacity.has_value());
    EXPECT_FALSE(cleared.pocketGrid.has_value());
}

// Every mutating verb hands back what storage now holds, so a GUI keeping a by-value
// binder can replace it wholesale instead of patching the fields it knows about.
TEST(BinderServiceTest, UpdateReturnsThePersistedBinderIncludingItsBlanks) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {Region::Kanto});
    f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 2});

    const CardBinder updated = f.service.update(created.id, "Renamed", {Region::Kanto}, 360);

    EXPECT_EQ(updated.name, "Renamed");
    EXPECT_EQ(updated.capacity, 360);
    // The edit form doesn't own blanks, so they survive the save and come back with it.
    ASSERT_EQ(updated.pocketBlanks.size(), 1u);
    EXPECT_EQ(updated.pocketBlanks[0].blanks, 2);
}

// --- blank pockets ---------------------------------------------------------------

TEST(BinderServiceTest, InsertBlanksReturnsTheBinderCarryingTheNewGap) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {Region::Kanto, Region::Kalos});

    const CardBinder withGap =
        f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 1});

    ASSERT_EQ(withGap.pocketBlanks.size(), 1u);
    EXPECT_EQ(withGap.pocketBlanks[0].beforeDexNum, 650);
    EXPECT_EQ(withGap.pocketBlanks[0].blanks, 1);
}

// The motivating workflow: press "Insert blank" twice to push Kalos onto a fresh page.
TEST(BinderServiceTest, InsertBlanksTwiceAtOneAnchorWidensTheGap) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {});
    f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 1});
    const CardBinder twice =
        f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 1});

    ASSERT_EQ(twice.pocketBlanks.size(), 1u);
    EXPECT_EQ(twice.pocketBlanks[0].blanks, 2);
}

TEST(BinderServiceTest, RemoveBlanksNarrowsThenClearsTheGap) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {});
    f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 2});

    const CardBinder narrowed =
        f.service.removeBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 1});
    ASSERT_EQ(narrowed.pocketBlanks.size(), 1u);
    EXPECT_EQ(narrowed.pocketBlanks[0].blanks, 1);

    const CardBinder gone =
        f.service.removeBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 1});
    EXPECT_TRUE(gone.pocketBlanks.empty());
}

TEST(BinderServiceTest, BlankVerbsRejectAnAnchorNamingBothOrNeitherRow) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {});

    EXPECT_THROW(f.service.insertBlanks(created.id, CardBinderBlank{.blanks = 1}), BinderError);
    EXPECT_THROW(f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650,
                                                                   .beforeCopyId = "c1",
                                                                   .blanks = 1}),
                 BinderError);
    EXPECT_THROW(
        f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 0}),
        BinderError);
    EXPECT_THROW(
        f.service.removeBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = -1}),
        BinderError);
}

TEST(BinderServiceTest, MutatingVerbsThrowForAMissingBinder) {
    Fixture f;
    EXPECT_THROW(f.service.insertBlanks("ghost", CardBinderBlank{.beforeDexNum = 1, .blanks = 1}),
                 std::exception);
    EXPECT_THROW(f.service.update("ghost", "New", {}), std::exception);
}

// --- moving a card ---------------------------------------------------------------------
//
// The move verbs are exercised end to end against a real guide in
// binder_move_planner_test.cpp; these pin the service's own contract.

// A card must be filed before it can be placed, so these drive the copy row directly
// rather than pulling in the copy service — the placement's foreign key needs it to exist.
void fileCopy(Database& db, const char* copyId, const char* binderId) {
    pokedex::Statement stmt(
        db,
        "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
        "ownership,condition,binder_id,comments,inserted_at,updated_at)"
        " VALUES(?,25,'MEW','EN','1/1','Owned','NearMint',?,'',"
        "'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");
    stmt.bindText(1, copyId);
    stmt.bindText(2, binderId);
    stmt.step();
}

TEST(BinderServiceTest, ApplyMoveWritesThePlacementAndItsBlankRunsTogether) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {});
    fileCopy(f.db, "c1", created.id.c_str());
    f.service.insertBlanks(created.id, CardBinderBlank{.beforeDexNum = 650, .blanks = 2});

    pokedex::BinderMovePlan plan;
    plan.cardCopyId = "c1";
    plan.placement = pokedex::CardBinderPlacement{.cardCopyId = "c1", .beforeDexNum = 650};
    plan.blankSets = {{.beforeDexNum = 650, .blanks = 0}, {.beforeCopyId = "c1", .blanks = 1}};

    const CardBinder moved = f.service.applyMove(created.id, plan);

    ASSERT_EQ(moved.cardPlacements.size(), 1u);
    EXPECT_EQ(moved.cardPlacements[0].cardCopyId, "c1");
    EXPECT_EQ(moved.cardPlacements[0].beforeDexNum, 650);
    // The species run was cleared and the surviving pocket re-anchored onto the card.
    ASSERT_EQ(moved.pocketBlanks.size(), 1u);
    EXPECT_EQ(moved.pocketBlanks[0].beforeCopyId, "c1");
    EXPECT_EQ(moved.pocketBlanks[0].blanks, 1);
}

// The load-bearing atomicity guard: a rejected blank set must take the placement down with
// it, or the binder would record a card moved into a gap that was never opened.
TEST(BinderServiceTest, ApplyMoveRollsBackWhollyWhenABlankSetIsInvalid) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {});
    fileCopy(f.db, "c1", created.id.c_str());

    pokedex::BinderMovePlan plan;
    plan.cardCopyId = "c1";
    plan.placement = pokedex::CardBinderPlacement{.cardCopyId = "c1", .beforeDexNum = 650};
    plan.blankSets = {{.beforeDexNum = 650, .beforeCopyId = "c1", .blanks = 1}};  // both anchors

    EXPECT_THROW(f.service.applyMove(created.id, plan), std::exception);

    const CardBinder after = f.service.list().front();
    EXPECT_TRUE(after.cardPlacements.empty());  // the placement went back too
    EXPECT_TRUE(after.pocketBlanks.empty());
}

// A plan with no placement is the reset shape — the card is named by the plan itself.
TEST(BinderServiceTest, ApplyMoveWithNoPlacementClearsTheCardsPosition) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {});
    fileCopy(f.db, "c1", created.id.c_str());
    pokedex::BinderMovePlan placeIt;
    placeIt.cardCopyId = "c1";
    placeIt.placement = pokedex::CardBinderPlacement{.cardCopyId = "c1", .beforeDexNum = 650};
    placeIt.blankSets = {{.beforeCopyId = "c1", .blanks = 2}};
    f.service.applyMove(created.id, placeIt);

    pokedex::BinderMovePlan reset;
    reset.cardCopyId = "c1";
    reset.blankSets = {{.beforeCopyId = "c1", .blanks = 0}};
    const CardBinder after = f.service.applyMove(created.id, reset);

    EXPECT_TRUE(after.cardPlacements.empty());
    EXPECT_TRUE(after.pocketBlanks.empty());
}

TEST(BinderServiceTest, ApplyMoveRejectsAnIncoherentPlan) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto+Kalos", {});

    pokedex::BinderMovePlan nameless;  // cardCopyId left empty
    EXPECT_THROW(f.service.applyMove(created.id, nameless), BinderError);

    // A placement naming a different card than the plan does would arrange the wrong one.
    pokedex::BinderMovePlan mismatched;
    mismatched.cardCopyId = "c1";
    mismatched.placement = pokedex::CardBinderPlacement{.cardCopyId = "c2", .beforeDexNum = 650};
    EXPECT_THROW(f.service.applyMove(created.id, mismatched), BinderError);
}

}  // namespace
