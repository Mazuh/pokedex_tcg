#include "core/app/binder_service.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

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

TEST(BinderServiceTest, CreateStampsIdRegionAndTimes) {
    Fixture f;
    const CardBinder created = f.service.create("Kanto Journey", Region::Kanto);

    EXPECT_EQ(created.id, "id-1");
    EXPECT_EQ(created.name, "Kanto Journey");
    ASSERT_TRUE(created.pokemonRegion.has_value());
    EXPECT_EQ(*created.pokemonRegion, Region::Kanto);
    EXPECT_EQ(created.insertedAt, f.now);
    EXPECT_EQ(created.updatedAt, f.now);

    // And it was persisted.
    const auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].id, "id-1");
}

TEST(BinderServiceTest, CreateWithoutRegionLeavesItUnset) {
    Fixture f;
    const CardBinder created = f.service.create("Loose Cards", std::nullopt);
    EXPECT_FALSE(created.pokemonRegion.has_value());
}

TEST(BinderServiceTest, EachCreateGetsADistinctId) {
    Fixture f;
    const CardBinder a = f.service.create("A", std::nullopt);
    const CardBinder b = f.service.create("B", std::nullopt);
    EXPECT_NE(a.id, b.id);
}

TEST(BinderServiceTest, CreateTrimsNameAndRejectsBlank) {
    Fixture f;
    EXPECT_EQ(f.service.create("  Spaced  ", std::nullopt).name, "Spaced");
    EXPECT_THROW(f.service.create("   ", std::nullopt), BinderError);
    EXPECT_THROW(f.service.create("", std::nullopt), BinderError);
}

TEST(BinderServiceTest, RenameChangesNameAndBumpsUpdatedAtOnly) {
    Fixture f;
    const CardBinder created = f.service.create("Old", Region::Kanto);

    f.now = at("2026-07-20T15:00:00Z");
    f.service.rename(created.id, "New");

    const auto binders = f.service.list();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].name, "New");
    EXPECT_EQ(binders[0].updatedAt, at("2026-07-20T15:00:00Z"));
    EXPECT_EQ(binders[0].insertedAt, at("2026-07-14T09:00:00Z"));
    // Region is fixed at creation — rename never touches it.
    ASSERT_TRUE(binders[0].pokemonRegion.has_value());
    EXPECT_EQ(*binders[0].pokemonRegion, Region::Kanto);
}

TEST(BinderServiceTest, RenameRejectsBlank) {
    Fixture f;
    const CardBinder created = f.service.create("Keep", std::nullopt);
    EXPECT_THROW(f.service.rename(created.id, "  "), BinderError);
}

TEST(BinderServiceTest, RenameMissingBinderThrows) {
    Fixture f;
    EXPECT_THROW(f.service.rename("does-not-exist", "New"), pokedex::StorageError);
}

TEST(BinderServiceTest, RemoveDropsTheBinderFromTheList) {
    Fixture f;
    const CardBinder a = f.service.create("A", std::nullopt);
    f.service.create("B", std::nullopt);

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

    const CardBinder a = service.create("A", std::nullopt);
    const CardBinder b = service.create("B", std::nullopt);
    EXPECT_FALSE(a.id.empty());
    EXPECT_NE(a.id, b.id);
}

}  // namespace
