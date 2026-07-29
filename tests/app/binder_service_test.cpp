#include "core/app/binder_service.h"

#include <gtest/gtest.h>

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

}  // namespace
