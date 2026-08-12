#include "core/storage/card_binder_repository.h"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/region.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace {

using pokedex::CardBinder;
using pokedex::CardBinderBlank;
using pokedex::CardBinderPocketGrid;
using pokedex::CardBinderRepository;
using pokedex::Database;
using pokedex::Region;
using pokedex::Statement;
using pokedex::Timestamp;

Timestamp at(const char* iso) { return pokedex::timestampFromIso(iso); }

CardBinder makeBinder(std::string id, std::string name, std::vector<Region> regions,
                      const char* stamp) {
    CardBinder binder;
    binder.id = std::move(id);
    binder.name = std::move(name);
    binder.pokemonRegions = std::move(regions);
    binder.insertedAt = at(stamp);
    binder.updatedAt = at(stamp);
    return binder;
}

TEST(CardBinderRepositoryTest, AddThenListRoundTrips) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);

    // b1 spans two regions; the input order is intentionally non-canonical (Johto
    // before Kanto) so the test pins that listAll returns them in canonical order.
    repo.add(makeBinder("b1", "Kanto+Johto", {Region::Johto, Region::Kanto},
                        "2026-07-14T09:00:00Z"));
    repo.add(makeBinder("b2", "Loose Cards", {}, "2026-07-14T10:30:00Z"));

    const auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 2u);

    // Ordered by inserted_at: b1 (09:00) before b2 (10:30).
    EXPECT_EQ(binders[0].id, "b1");
    EXPECT_EQ(binders[0].name, "Kanto+Johto");
    EXPECT_EQ(binders[0].pokemonRegions,
              (std::vector<Region>{Region::Kanto, Region::Johto}));  // canonical order
    EXPECT_EQ(binders[0].insertedAt, at("2026-07-14T09:00:00Z"));
    EXPECT_EQ(binders[0].updatedAt, at("2026-07-14T09:00:00Z"));

    EXPECT_EQ(binders[1].id, "b2");
    EXPECT_TRUE(binders[1].pokemonRegions.empty());  // no region rows
}

// Two binders created within the same (second-precision) second must still come
// back in creation order — the id tiebreaker is a random UUID, so ordering has
// to fall back to insertion order (rowid), not id.
TEST(CardBinderRepositoryTest, ListPreservesCreationOrderWithinSameSecond) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    // Ids chosen so that ordering-by-id would reverse them (z before a).
    repo.add(makeBinder("zzz", "First", {}, "2026-07-14T09:00:00Z"));
    repo.add(makeBinder("aaa", "Second", {}, "2026-07-14T09:00:00Z"));

    const auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 2u);
    EXPECT_EQ(binders[0].name, "First");
    EXPECT_EQ(binders[1].name, "Second");
}

// A region token the codec doesn't recognize (a hand-edited or newer-schema row)
// must not abort the whole listing — the binder still shows, with only its
// recognized regions.
TEST(CardBinderRepositoryTest, ListToleratesUnknownRegionToken) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Mystery", {}, "2026-07-14T09:00:00Z"));
    // One valid and one bogus region row, inserted directly into the join table.
    db.exec("INSERT INTO card_binder_region(binder_id,region) VALUES('b1','Kanto');");
    db.exec("INSERT INTO card_binder_region(binder_id,region) VALUES('b1','Atlantis');");

    std::vector<CardBinder> binders;
    ASSERT_NO_THROW(binders = repo.listAll());
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].name, "Mystery");
    EXPECT_EQ(binders[0].pokemonRegions, (std::vector<Region>{Region::Kanto}));
}

TEST(CardBinderRepositoryTest, UpdateThrowsForMissingId) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    EXPECT_THROW(repo.update("ghost", "New", {}, std::nullopt, std::nullopt,
                             at("2026-07-15T12:00:00Z")),
                 pokedex::StorageError);
}

TEST(CardBinderRepositoryTest, UpdateChangesNameAndStamp) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Old Name", {}, "2026-07-14T09:00:00Z"));

    repo.update("b1", "New Name", {}, std::nullopt, std::nullopt, at("2026-07-15T12:00:00Z"));

    const auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].name, "New Name");
    EXPECT_EQ(binders[0].updatedAt, at("2026-07-15T12:00:00Z"));
    EXPECT_EQ(binders[0].insertedAt, at("2026-07-14T09:00:00Z"));  // untouched
}

// A binder's regions are chosen once, at add(), and update() cannot reach them. They
// decide which species get a reserved slot and hence where every page falls, so
// re-scoping a binder that already holds cards would re-derive the whole layout under
// the blanks and moves the user arranged against it.
// While a binder is still EMPTY its regions are just a setting, so a scope typed wrong at
// creation stays correctable.
TEST(CardBinderRepositoryTest, UpdateCanChangeTheRegionsOfAnEmptyBinder) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto Journey", {Region::Kanto}, "2026-07-14T09:00:00Z"));

    repo.update("b1", "Johto Journey", {Region::Johto}, std::nullopt, std::nullopt,
                at("2026-07-15T12:00:00Z"));

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->pokemonRegions, (std::vector<Region>{Region::Johto}));
}

// Once anything is filed in or arranged about it, the regions are settled: they decide
// which species get a reserved slot and hence where every page falls, so re-deriving them
// would move the pages out from under the blanks and moves recorded against them.
TEST(CardBinderRepositoryTest, UpdateRefusesToRescopeABinderThatHoldsSomething) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto Journey", {Region::Kanto}, "2026-07-14T09:00:00Z"));
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 25, .blanks = 1});

    EXPECT_THROW(repo.update("b1", "Renamed", {Region::Johto}, std::nullopt, std::nullopt,
                             at("2026-07-15T12:00:00Z")),
                 pokedex::StorageError);

    // …and the refusal rolls back the whole edit rather than half-applying the name.
    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Kanto Journey");
    EXPECT_EQ(found->pokemonRegions, (std::vector<Region>{Region::Kanto}));

    // Saving the SAME regions is not a change, so an ordinary rename still works.
    repo.update("b1", "Renamed", {Region::Kanto}, std::nullopt, std::nullopt,
                at("2026-07-15T12:00:00Z"));
    EXPECT_EQ(repo.find("b1")->name, "Renamed");
}

TEST(CardBinderRepositoryTest, HasContentsSeesCardsBlanksAndPlacements) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Empty", {}, "2026-07-14T09:00:00Z"));
    EXPECT_FALSE(repo.hasContents("b1"));

    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 25, .blanks = 1});
    EXPECT_TRUE(repo.hasContents("b1"));
}

// The album's physical layout round-trips through add/listAll. A binder that records
// neither reads back as unset, not as some invented default.
TEST(CardBinderRepositoryTest, AddThenListRoundTripsCapacityAndPocketGrid) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);

    CardBinder measured = makeBinder("b1", "Kanto Journey", {}, "2026-07-14T09:00:00Z");
    measured.capacity = 360;
    measured.pocketGrid = CardBinderPocketGrid{.rows = 3, .columns = 3};
    repo.add(measured);
    repo.add(makeBinder("b2", "Unmeasured", {}, "2026-07-14T10:00:00Z"));

    const auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 2u);
    EXPECT_EQ(binders[0].capacity, 360);
    ASSERT_TRUE(binders[0].pocketGrid.has_value());
    EXPECT_EQ(binders[0].pocketGrid->rows, 3);
    EXPECT_EQ(binders[0].pocketGrid->columns, 3);
    EXPECT_FALSE(binders[1].capacity.has_value());
    EXPECT_FALSE(binders[1].pocketGrid.has_value());
}

// 0 is the storage sentinel for "unset" (see the v12 migration), which is also what
// every binder that predates the columns holds.
TEST(CardBinderRepositoryTest, ListDecodesZeroLayoutColumnsAsUnset) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    db.exec(
        "INSERT INTO card_binder(id,name,region,capacity,pocket_rows,pocket_columns,"
        "inserted_at,updated_at)"
        " VALUES('b1','Legacy',NULL,0,0,0,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");

    const auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_FALSE(binders[0].capacity.has_value());
    EXPECT_FALSE(binders[0].pocketGrid.has_value());
}

// A grid needs both sides to mean anything; half of one describes no album and would
// divide by zero downstream, so it decodes as unset rather than as a 3×0 grid.
TEST(CardBinderRepositoryTest, ListDecodesAHalfSetGridAsUnset) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    db.exec(
        "INSERT INTO card_binder(id,name,region,capacity,pocket_rows,pocket_columns,"
        "inserted_at,updated_at)"
        " VALUES('b1','Half',NULL,0,3,0,'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");

    const auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_FALSE(binders[0].pocketGrid.has_value());
}

TEST(CardBinderRepositoryTest, UpdateChangesTheLayoutAndCanClearIt) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto Journey", {}, "2026-07-14T09:00:00Z"));

    repo.update("b1", "Kanto Journey", {}, 360, CardBinderPocketGrid{.rows = 3, .columns = 3},
                at("2026-07-15T12:00:00Z"));
    auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].capacity, 360);
    ASSERT_TRUE(binders[0].pocketGrid.has_value());
    EXPECT_EQ(pocketsPerPage(*binders[0].pocketGrid), 9);

    repo.update("b1", "Kanto Journey", {}, std::nullopt, std::nullopt,
                at("2026-07-16T12:00:00Z"));
    binders = repo.listAll();
    EXPECT_FALSE(binders[0].capacity.has_value());
    EXPECT_FALSE(binders[0].pocketGrid.has_value());
}

TEST(CardBinderRepositoryTest, FindReturnsTheBinderWithItsRegionsAndBlanks) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {Region::Kalos, Region::Kanto},
                        "2026-07-14T09:00:00Z"));
    repo.add(makeBinder("b2", "Other", {}, "2026-07-14T10:00:00Z"));
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 2});
    repo.addBlanks("b2", CardBinderBlank{.beforeDexNum = 1, .blanks = 5});

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Kanto+Kalos");
    EXPECT_EQ(found->pokemonRegions, (std::vector<Region>{Region::Kanto, Region::Kalos}));
    // Scoped to this binder: b2's blank must not leak in.
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].beforeDexNum, 650);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 2);
}

TEST(CardBinderRepositoryTest, FindReturnsNulloptForAMissingId) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    EXPECT_FALSE(repo.find("ghost").has_value());
}

// Inserting twice at one anchor widens the gap rather than failing on the primary key
// — pressing "Insert blank" twice is how the user pads out a page.
TEST(CardBinderRepositoryTest, AddBlanksAccumulatesAtTheSameAnchor) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));

    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 1});
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 1});

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 2);
}

TEST(CardBinderRepositoryTest, BlanksAnchorToEitherASpeciesOrAnExactCard) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Mixed", {}, "2026-07-14T09:00:00Z"));

    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 2});
    repo.addBlanks("b1", CardBinderBlank{.beforeCopyId = "copy-7", .blanks = 1});

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->pocketBlanks.size(), 2u);
    // Ordered by anchor: the copy-anchored row (dex 0) sorts before the species one.
    EXPECT_EQ(found->pocketBlanks[0].beforeCopyId, "copy-7");
    EXPECT_FALSE(found->pocketBlanks[0].beforeDexNum.has_value());
    EXPECT_EQ(found->pocketBlanks[1].beforeDexNum, 650);
    EXPECT_FALSE(found->pocketBlanks[1].beforeCopyId.has_value());
}

TEST(CardBinderRepositoryTest, RemoveBlanksDecrementsThenDropsTheAnchor) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 2});

    repo.removeBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 1});
    auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 1);

    // The last one takes the row with it, rather than leaving a zero the reader skips.
    repo.removeBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 1});
    found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->pocketBlanks.empty());
    Statement rows(db, "SELECT COUNT(*) FROM card_binder_blank;");
    ASSERT_TRUE(rows.step());
    EXPECT_EQ(rows.columnInt(0), 0);
}

TEST(CardBinderRepositoryTest, RemoveBlanksIsANoOpAtAnAnchorHoldingNone) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));

    EXPECT_NO_THROW(repo.removeBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 1}));
    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->pocketBlanks.empty());
}

// A blank that names both anchors, or neither, could never be placed in the guide —
// reject it on write rather than storing a row the reader will silently drop.
TEST(CardBinderRepositoryTest, BlankVerbsRejectAnUnplaceableAnchor) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));

    EXPECT_THROW(repo.addBlanks("b1", CardBinderBlank{.blanks = 1}), pokedex::StorageError);
    EXPECT_THROW(
        repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .beforeCopyId = "c", .blanks = 1}),
        pokedex::StorageError);
    EXPECT_THROW(repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 0}),
                 pokedex::StorageError);
}

// Mirrors ListToleratesUnknownRegionToken: a hand-edited row that names both anchors,
// neither, or no pockets is dropped without taking the binder down with it.
TEST(CardBinderRepositoryTest, ListSkipsUnplaceableBlankRows) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Mystery", {}, "2026-07-14T09:00:00Z"));
    db.exec(
        "INSERT INTO card_binder_blank(binder_id,before_dex_num,before_copy_id,blanks)"
        " VALUES('b1',650,'copy-1',2);");  // both anchors
    db.exec(
        "INSERT INTO card_binder_blank(binder_id,before_dex_num,before_copy_id,blanks)"
        " VALUES('b1',0,'',2);");  // neither anchor
    db.exec(
        "INSERT INTO card_binder_blank(binder_id,before_dex_num,before_copy_id,blanks)"
        " VALUES('b1',151,'',0);");  // no pockets
    db.exec(
        "INSERT INTO card_binder_blank(binder_id,before_dex_num,before_copy_id,blanks)"
        " VALUES('b1',650,'',2);");  // the one good row

    std::vector<CardBinder> binders;
    ASSERT_NO_THROW(binders = repo.listAll());
    ASSERT_EQ(binders.size(), 1u);
    ASSERT_EQ(binders[0].pocketBlanks.size(), 1u);
    EXPECT_EQ(binders[0].pocketBlanks[0].beforeDexNum, 650);
}

// The load-bearing guard: update() is the name/regions/layout form's write path and
// must leave the blank set alone. If it ever took a whole CardBinder and rewrote the
// blanks, a save from a stale in-memory binder would wipe blanks added since it was read.
TEST(CardBinderRepositoryTest, UpdateLeavesTheBlankSetUntouched) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {Region::Kanto}, "2026-07-14T09:00:00Z"));
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 2});

    repo.update("b1", "Renamed", {Region::Kanto}, 360, CardBinderPocketGrid{.rows = 3, .columns = 3},
                at("2026-07-15T12:00:00Z"));

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Renamed");
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].beforeDexNum, 650);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 2);
}

// Blanks round-trip through add() too, so a whole CardBinder value survives a write.
TEST(CardBinderRepositoryTest, AddWritesTheBlankSetItIsGiven) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    CardBinder binder = makeBinder("b1", "Restored", {}, "2026-07-14T09:00:00Z");
    binder.pocketBlanks = {CardBinderBlank{.beforeDexNum = 650, .blanks = 2}};
    repo.add(binder);

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 2);
}

// --- setBlanks: the ABSOLUTE write a move uses -------------------------------------

// It both opens a gap where there was no row and overwrites an existing one, so a
// caller stating final counts never has to know which case it is in.
TEST(CardBinderRepositoryTest, SetBlanksWritesTheStatedCountEitherWay) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));

    repo.setBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 3});  // creates
    auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 3);

    // Overwrites rather than accumulating — this is what separates it from addBlanks.
    repo.setBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 1});
    found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 1);
}

// Zero is a legal count here (and only here): it is how a move that consumed the last
// pocket of a run closes the gap. The row is dropped rather than left at 0.
TEST(CardBinderRepositoryTest, SetBlanksToZeroDropsTheAnchorRow) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 2});

    repo.setBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 0});

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->pocketBlanks.empty());
    Statement stmt(db, "SELECT COUNT(*) FROM card_binder_blank;");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnInt(0), 0);  // dropped, not left at zero
}

TEST(CardBinderRepositoryTest, SetBlanksRejectsAnUnplaceableAnchorOrNegativeCount) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Mystery", {}, "2026-07-14T09:00:00Z"));

    EXPECT_THROW(repo.setBlanks("b1", CardBinderBlank{.blanks = 1}), pokedex::StorageError);
    EXPECT_THROW(repo.setBlanks("b1", CardBinderBlank{.beforeDexNum = 650,
                                                      .beforeCopyId = "c1",
                                                      .blanks = 1}),
                 pokedex::StorageError);
    EXPECT_THROW(repo.setBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = -1}),
                 pokedex::StorageError);
}

// --- placements ---------------------------------------------------------------------

// A copy filed in b1, needed because card_binder_placement.card_copy_id carries a
// foreign key into card_copy (unlike a blank's before_copy_id, which may be '').
void insertCopy(Database& db, const char* id, int dexNum) {
    Statement stmt(db,
                   "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,"
                   "ref_collector,ownership,condition,binder_id,comments,inserted_at,"
                   "updated_at) VALUES(?,?,'MEW','EN','1/1','Owned','NearMint','b1','',"
                   "'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");
    stmt.bindText(1, id);
    stmt.bindInt(2, dexNum);
    stmt.step();
}

// All three anchor shapes round-trip, including the one a blank cannot express: NEITHER
// half set, meaning "at the very end".
TEST(CardBinderRepositoryTest, PlacementsRoundTripAllThreeAnchorShapes) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);
    insertCopy(db, "c2", 26);
    insertCopy(db, "c3", 0);
    insertCopy(db, "anchor", 650);

    repo.setPlacement("b1", {.cardCopyId = "c1", .beforeCopyId = "anchor", .ordinal = 1});
    repo.setPlacement("b1", {.cardCopyId = "c2", .beforeDexNum = 650});
    repo.setPlacement("b1", {.cardCopyId = "c3"});  // at the very end

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->cardPlacements.size(), 3u);

    // Ordered by (before_dex_num, before_copy_id, ordinal): the end sentinel (0,'')
    // first, then the copy anchor (0,'anchor'), then the dex anchor (650,'').
    EXPECT_EQ(found->cardPlacements[0].cardCopyId, "c3");
    EXPECT_FALSE(found->cardPlacements[0].beforeDexNum.has_value());
    EXPECT_FALSE(found->cardPlacements[0].beforeCopyId.has_value());

    EXPECT_EQ(found->cardPlacements[1].cardCopyId, "c1");
    EXPECT_EQ(found->cardPlacements[1].beforeCopyId, "anchor");
    EXPECT_EQ(found->cardPlacements[1].ordinal, 1);

    EXPECT_EQ(found->cardPlacements[2].cardCopyId, "c2");
    EXPECT_EQ(found->cardPlacements[2].beforeDexNum, 650);
}

// A copy has at most ONE placement per binder, so re-placing a moved card overwrites
// its anchor instead of colliding on the primary key or leaving two rows.
TEST(CardBinderRepositoryTest, SetPlacementReplacesTheCopysPreviousAnchor) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);

    repo.setPlacement("b1", {.cardCopyId = "c1", .beforeDexNum = 650, .ordinal = 2});
    repo.setPlacement("b1", {.cardCopyId = "c1", .beforeDexNum = 151});

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->cardPlacements.size(), 1u);
    EXPECT_EQ(found->cardPlacements[0].beforeDexNum, 151);
    EXPECT_EQ(found->cardPlacements[0].ordinal, 0);
}

TEST(CardBinderRepositoryTest, ClearPlacementReturnsACopyToNaturalOrder) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);
    repo.setPlacement("b1", {.cardCopyId = "c1", .beforeDexNum = 650});

    repo.clearPlacement("b1", "c1");
    EXPECT_TRUE(repo.find("b1")->cardPlacements.empty());

    ASSERT_NO_THROW(repo.clearPlacement("b1", "c1"));  // a no-op the second time
}

TEST(CardBinderRepositoryTest, PlacementVerbsRejectAnUnplaceableRow) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Mystery", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);

    EXPECT_THROW(repo.setPlacement("b1", {.cardCopyId = ""}), pokedex::StorageError);
    // Both anchors names no single row — the one combination a placement forbids.
    EXPECT_THROW(repo.setPlacement("b1", {.cardCopyId = "c1",
                                          .beforeDexNum = 650,
                                          .beforeCopyId = "c2"}),
                 pokedex::StorageError);
    EXPECT_THROW(repo.setPlacement("b1", {.cardCopyId = "c1", .ordinal = -1}),
                 pokedex::StorageError);
}

TEST(CardBinderRepositoryTest, ListSkipsUnplaceablePlacementRows) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Mystery", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);
    insertCopy(db, "c2", 26);
    db.exec(
        "INSERT INTO card_binder_placement(binder_id,card_copy_id,before_dex_num,"
        "before_copy_id,ordinal) VALUES('b1','c1',650,'anchor',0);");  // both anchors
    db.exec(
        "INSERT INTO card_binder_placement(binder_id,card_copy_id,before_dex_num,"
        "before_copy_id,ordinal) VALUES('b1','c2',650,'',0);");  // the one good row

    std::vector<CardBinder> binders;
    ASSERT_NO_THROW(binders = repo.listAll());
    ASSERT_EQ(binders.size(), 1u);
    ASSERT_EQ(binders[0].cardPlacements.size(), 1u);
    EXPECT_EQ(binders[0].cardPlacements[0].cardCopyId, "c2");
}

// The same load-bearing guard the blank set has: the name/regions/layout form must not
// clobber a manual arrangement recorded since its binder was read.
TEST(CardBinderRepositoryTest, UpdateLeavesTheManualArrangementUntouched) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {Region::Kanto}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 2});
    repo.setPlacement("b1", {.cardCopyId = "c1", .beforeDexNum = 650});

    repo.update("b1", "Renamed", {Region::Kanto}, 360, CardBinderPocketGrid{.rows = 3, .columns = 3},
                at("2026-07-15T12:00:00Z"));

    const auto found = repo.find("b1");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->name, "Renamed");
    ASSERT_EQ(found->pocketBlanks.size(), 1u);
    EXPECT_EQ(found->pocketBlanks[0].blanks, 2);
    ASSERT_EQ(found->cardPlacements.size(), 1u);
    EXPECT_EQ(found->cardPlacements[0].cardCopyId, "c1");
}

TEST(CardBinderRepositoryTest, AddWritesThePlacementsItIsGiven) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Host", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);
    // A second binder, so the copy that b2's placement names already exists.
    CardBinder b2 = makeBinder("b2", "Restored", {}, "2026-07-14T10:00:00Z");
    b2.cardPlacements = {{.cardCopyId = "c1", .beforeDexNum = 650, .ordinal = 1}};
    repo.add(b2);

    const auto found = repo.find("b2");
    ASSERT_TRUE(found.has_value());
    ASSERT_EQ(found->cardPlacements.size(), 1u);
    EXPECT_EQ(found->cardPlacements[0].beforeDexNum, 650);
    EXPECT_EQ(found->cardPlacements[0].ordinal, 1);
}

// Unlike a blank's before_copy_id (which may be '' and so carries no key), a
// placement's card_copy_id references card_copy — so hard-deleting the card takes its
// placement with it rather than leaving an inert row behind.
TEST(CardBinderRepositoryTest, HardDeletingACopyCascadesItsPlacement) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);
    repo.setPlacement("b1", {.cardCopyId = "c1", .beforeDexNum = 650});

    db.exec("DELETE FROM card_copy WHERE id = 'c1';");

    EXPECT_TRUE(repo.find("b1")->cardPlacements.empty());
}

TEST(CardBinderRepositoryTest, RemoveCascadesPlacementRows) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    insertCopy(db, "c1", 25);
    repo.setPlacement("b1", {.cardCopyId = "c1", .beforeDexNum = 650});

    repo.remove("b1");

    Statement stmt(db, "SELECT COUNT(*) FROM card_binder_placement;");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnInt(0), 0);
}

TEST(CardBinderRepositoryTest, RemoveDeletesTheRow) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Doomed", {}, "2026-07-14T09:00:00Z"));

    repo.remove("b1");

    EXPECT_TRUE(repo.listAll().empty());
}

// Removing a binder cascades its region rows away (card_binder_region has ON
// DELETE CASCADE) — no orphan rows linger to attach to a re-used id.
TEST(CardBinderRepositoryTest, RemoveCascadesRegionRows) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Johto", {Region::Kanto, Region::Johto},
                        "2026-07-14T09:00:00Z"));

    repo.remove("b1");

    Statement stmt(db, "SELECT COUNT(*) FROM card_binder_region;");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnInt(0), 0);
}

// Same for its blank rows — card_binder_blank has ON DELETE CASCADE too, so a re-used
// id can't inherit a stranger's page breaks.
TEST(CardBinderRepositoryTest, RemoveCascadesBlankRows) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto+Kalos", {}, "2026-07-14T09:00:00Z"));
    repo.addBlanks("b1", CardBinderBlank{.beforeDexNum = 650, .blanks = 2});

    repo.remove("b1");

    Statement stmt(db, "SELECT COUNT(*) FROM card_binder_blank;");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnInt(0), 0);
}

// The central contract: removing a binder must NOT delete cards filed in it. The
// schema's ON DELETE SET NULL clears the association; the copy survives.
TEST(CardBinderRepositoryTest, RemoveKeepsFiledCardsAndClearsTheirBinder) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Kanto Journey", {Region::Kanto}, "2026-07-14T09:00:00Z"));

    // A copy filed under b1, inserted directly (card_copy has no repository yet).
    db.exec(
        "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
        "ownership,condition,binder_id,comments,inserted_at,updated_at)"
        " VALUES('c1',25,'MEW','EN','151/165','Owned','NearMint','b1','',"
        "'2026-07-14T09:00:00Z','2026-07-14T09:00:00Z');");

    repo.remove("b1");

    Statement stmt(db, "SELECT binder_id FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());          // the copy still exists
    EXPECT_TRUE(stmt.columnIsNull(0));  // but its binder link was cleared
    EXPECT_FALSE(stmt.step());
}

}  // namespace
