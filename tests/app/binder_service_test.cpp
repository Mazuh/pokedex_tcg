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
    // Keep the regions the same; only the name changes here.
    f.service.update(created.id, "New", {Region::Kanto});

    const auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].name, "New");
    EXPECT_EQ(binders[0].updatedAt, at("2026-07-20T15:00:00Z"));
    EXPECT_EQ(binders[0].insertedAt, at("2026-07-14T09:00:00Z"));
    EXPECT_EQ(binders[0].pokemonRegions, (std::vector<Region>{Region::Kanto}));
}

TEST(BinderServiceTest, UpdateCanChangeTheRegions) {
    Fixture f;
    const CardBinder created = f.service.create("Old", {Region::Kanto});

    f.service.update(created.id, "Old", {Region::Johto, Region::Hoenn});
    auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    // listAll returns regions in canonical (enum) order regardless of input order.
    EXPECT_EQ(binders[0].pokemonRegions,
              (std::vector<Region>{Region::Johto, Region::Hoenn}));

    // And they can be cleared back to none.
    f.service.update(created.id, "Old", {});
    binders = f.service.list();
    EXPECT_TRUE(binders[0].pokemonRegions.empty());
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

}  // namespace
