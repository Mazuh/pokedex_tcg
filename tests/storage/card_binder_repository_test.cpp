#include "core/storage/card_binder_repository.h"

#include <gtest/gtest.h>

#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/region.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace {

using pokedex::CardBinder;
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
    EXPECT_THROW(repo.update("ghost", "New", {}, at("2026-07-15T12:00:00Z")),
                 pokedex::StorageError);
}

TEST(CardBinderRepositoryTest, UpdateChangesNameRegionsAndStamp) {
    Database db(":memory:");
    db.migrate();
    CardBinderRepository repo(db);
    repo.add(makeBinder("b1", "Old Name", {}, "2026-07-14T09:00:00Z"));

    repo.update("b1", "New Name", {Region::Kanto, Region::Johto},
                at("2026-07-15T12:00:00Z"));

    auto binders = repo.listAll();
    ASSERT_EQ(binders.size(), 1u);
    EXPECT_EQ(binders[0].name, "New Name");
    EXPECT_EQ(binders[0].pokemonRegions,
              (std::vector<Region>{Region::Kanto, Region::Johto}));
    EXPECT_EQ(binders[0].updatedAt, at("2026-07-15T12:00:00Z"));
    EXPECT_EQ(binders[0].insertedAt, at("2026-07-14T09:00:00Z"));  // untouched

    // A second update replaces the whole set — here, down to a single region.
    repo.update("b1", "New Name", {Region::Hoenn}, at("2026-07-16T12:00:00Z"));
    binders = repo.listAll();
    EXPECT_EQ(binders[0].pokemonRegions, (std::vector<Region>{Region::Hoenn}));

    // And it can be cleared back to none.
    repo.update("b1", "New Name", {}, at("2026-07-17T12:00:00Z"));
    binders = repo.listAll();
    EXPECT_TRUE(binders[0].pokemonRegions.empty());
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
