// test_env.h sets _DARWIN_C_SOURCE / _DEFAULT_SOURCE for setenv; include it
// before any system header so those declarations are visible under -std=c++23.
#include "../support/test_env.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace {

namespace fs = std::filesystem;

using pokedex::Database;
using pokedex::Statement;
using pokedex::StorageError;
using pokedex_test::TempDir;

TEST(DatabaseTest, FreshDatabaseHasNoSchemaVersion) {
    Database db(":memory:");
    EXPECT_EQ(db.userVersion(), 0);
}

TEST(DatabaseTest, MigrateStampsSchemaVersionAndIsIdempotent) {
    Database db(":memory:");
    db.migrate();
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    // A second migration is a no-op, not an error.
    EXPECT_NO_THROW(db.migrate());
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
}

// Upgrading an existing v1 database runs the whole tail of the chain (v2 then v3),
// adding card_copy.ref_set_name and card_copy.ref_name without losing data — so a
// user's pre-v2 file keeps working.
TEST(DatabaseTest, UpgradesAnExistingV1DatabaseThroughTheChain) {
    Database db(":memory:");
    // Stand up a v1 card_copy (no ref_set_name / ref_name) with a row, and stamp it v1.
    db.exec(
        "CREATE TABLE card_copy(id TEXT PRIMARY KEY, pokemon_dex_num INTEGER NOT NULL,"
        " ref_expansion TEXT NOT NULL, ref_language TEXT NOT NULL, ref_collector TEXT NOT NULL,"
        " ownership TEXT NOT NULL, condition TEXT NOT NULL, binder_id TEXT,"
        " comments TEXT NOT NULL DEFAULT '', inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.exec(
        "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
        "ownership,condition,binder_id,comments,inserted_at,updated_at)"
        " VALUES('c1',6,'OBF','EN','125/197','Owned','NearMint',NULL,'',"
        "'2026-07-16T00:00:00Z','2026-07-16T00:00:00Z');");
    // A v1 card_binder with a single region — the v11 step migrates it into the new
    // card_binder_region join table, so stand one up to prove the backfill.
    db.exec(
        "CREATE TABLE card_binder(id TEXT PRIMARY KEY, name TEXT NOT NULL, region TEXT,"
        " inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto Journey','Kanto','2026-07-16T00:00:00Z','2026-07-16T00:00:00Z');");
    db.setUserVersion(1);

    db.migrate();
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    // The pre-existing row survives and gains a blank set name, card name, and
    // (v8) external card id — the whole additive tail applies without data loss.
    Statement stmt(db,
                   "SELECT ref_set_name, ref_name, external_card_id FROM card_copy"
                   " WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "");
    EXPECT_EQ(stmt.columnText(1), "");
    EXPECT_EQ(stmt.columnText(2), "");  // unlinked by default

    // v11: the binder's single region was backfilled into card_binder_region.
    Statement region(db, "SELECT region FROM card_binder_region WHERE binder_id = 'b1';");
    ASSERT_TRUE(region.step());
    EXPECT_EQ(region.columnText(0), "Kanto");
    EXPECT_FALSE(region.step());  // exactly one region row
}

// The v2 → v3 upgrade adds card_copy.ref_name to an existing v2 file (one that
// already has ref_set_name) without losing data.
TEST(DatabaseTest, UpgradesAnExistingV2DatabaseByAddingCardName) {
    Database db(":memory:");
    // Stand up a v2 card_copy (has ref_set_name, no ref_name) with a row, stamp it v2.
    db.exec(
        "CREATE TABLE card_copy(id TEXT PRIMARY KEY, pokemon_dex_num INTEGER NOT NULL,"
        " ref_expansion TEXT NOT NULL, ref_language TEXT NOT NULL, ref_collector TEXT NOT NULL,"
        " ownership TEXT NOT NULL, condition TEXT NOT NULL, binder_id TEXT,"
        " comments TEXT NOT NULL DEFAULT '', inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL,"
        " ref_set_name TEXT NOT NULL DEFAULT '');");
    db.exec(
        "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
        "ownership,condition,binder_id,comments,inserted_at,updated_at,ref_set_name)"
        " VALUES('c1',6,'OBF','EN','125/197','Owned','NearMint',NULL,'',"
        "'2026-07-16T00:00:00Z','2026-07-16T00:00:00Z','Obsidian Flames');");
    // A card_binder table has existed since v1; the v11 step reads it, so stand up
    // the (empty) table this fixture otherwise omits.
    db.exec(
        "CREATE TABLE card_binder(id TEXT PRIMARY KEY, name TEXT NOT NULL, region TEXT,"
        " inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.setUserVersion(2);

    db.migrate();
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    // The pre-existing row survives, keeps its set name, and gains a blank card name.
    Statement stmt(db, "SELECT ref_set_name, ref_name FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "Obsidian Flames");
    EXPECT_EQ(stmt.columnText(1), "");
}

// v9 unifies the set cache across providers WITHOUT losing the existing (pokemontcg) rows —
// they are the disk fallback that keeps set-narrowing working during a /v2/sets outage, so an
// upgrade must preserve them (re-tagged) rather than drop them, and rename the fetch stamp.
TEST(DatabaseTest, UpgradesAnExistingV8SetCachePreservingPokemontcgRows) {
    Database db(":memory:");
    // Stand up the v8 set cache (id PRIMARY KEY, no source) + cache_meta, with a cached set and
    // its single fetch stamp, and mark the DB v8 so migrate() runs only the v9 step.
    db.exec(
        "CREATE TABLE card_set_cache(id TEXT PRIMARY KEY, ptcgo_code TEXT NOT NULL,"
        " name TEXT NOT NULL, printed_total INTEGER NOT NULL);");
    db.exec("CREATE TABLE cache_meta(key TEXT PRIMARY KEY, value TEXT NOT NULL);");
    db.exec(
        "INSERT INTO card_set_cache(id,ptcgo_code,name,printed_total)"
        " VALUES('sv3','OBF','Obsidian Flames',197);");
    db.exec("INSERT INTO cache_meta(key,value) VALUES('sets_fetched_at','2026-07-20T00:00:00Z');");
    // A card_binder table has existed since v1; the v11 step reads it, so stand up
    // the (empty) table this fixture otherwise omits.
    db.exec(
        "CREATE TABLE card_binder(id TEXT PRIMARY KEY, name TEXT NOT NULL, region TEXT,"
        " inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.setUserVersion(8);

    db.migrate();
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);

    // The pre-existing row survives, re-tagged source='pokemontcg'.
    Statement row(db, "SELECT source, name FROM card_set_cache WHERE id = 'sv3';");
    ASSERT_TRUE(row.step());
    EXPECT_EQ(row.columnText(0), "pokemontcg");
    EXPECT_EQ(row.columnText(1), "Obsidian Flames");

    // The old fetch stamp is renamed to the per-source key (not orphaned, keeping its age).
    Statement stamp(db, "SELECT value FROM cache_meta WHERE key = 'sets_fetched_at:pokemontcg';");
    ASSERT_TRUE(stamp.step());
    EXPECT_EQ(stamp.columnText(0), "2026-07-20T00:00:00Z");
    Statement orphan(db, "SELECT COUNT(*) FROM cache_meta WHERE key = 'sets_fetched_at';");
    ASSERT_TRUE(orphan.step());
    EXPECT_EQ(orphan.columnInt(0), 0);

    // The new (source, id) primary key lets another provider reuse the same id string.
    EXPECT_NO_THROW(
        db.exec("INSERT INTO card_set_cache(source,id,ptcgo_code,name,printed_total)"
                " VALUES('tcgdex','sv3','','Obsidian Flames (tcgdex)',230);"));
}

// v10 adds card_price_suppression alongside the existing price cache; a DB already at v9
// gains just that table, leaving its prior data intact.
TEST(DatabaseTest, UpgradesAnExistingV9DatabaseByAddingPriceSuppression) {
    Database db(":memory:");
    // Stand up the one pre-existing table the v10..v13 tail touches — the v1-shaped
    // card_binder (v11 backfills its region into a join table, v12 ALTERs it, v13
    // references it) — then mark the file v9 so migrate() runs only that tail.
    //
    // Building the fixture BY HAND rather than migrating a fresh DB and rolling the stamp
    // back is deliberate, and the general rule now that a step ALTERs a v1 table:
    // migrate-then-replay is no longer safe. Re-running v12 over a table that already has
    // the column fails with "duplicate column name", and an ADD COLUMN can't be undone
    // without DROP COLUMN, whose availability varies with the system SQLite on either CI leg.
    db.exec(
        "CREATE TABLE card_binder(id TEXT PRIMARY KEY, name TEXT NOT NULL, region TEXT,"
        " inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto Journey','Kanto','2026-07-14T00:00:00Z','2026-07-14T00:00:00Z');");
    db.setUserVersion(9);

    db.migrate();
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);

    // The pre-existing binder survives the tail untouched.
    Statement binder(db, "SELECT name FROM card_binder WHERE id = 'b1';");
    ASSERT_TRUE(binder.step());
    EXPECT_EQ(binder.columnText(0), "Kanto Journey");

    // The table exists with its (external_card_id, provenance) primary key: a row inserts,
    // a duplicate is rejected.
    EXPECT_NO_THROW(db.exec("INSERT INTO card_price_suppression(external_card_id,provenance)"
                            " VALUES('sv3-125','tcgplayer');"));
    EXPECT_ANY_THROW(db.exec("INSERT INTO card_price_suppression(external_card_id,provenance)"
                             " VALUES('sv3-125','tcgplayer');"));
}

// v12/v13/v14 give a binder its optional physical layout, its blank pockets and its moved
// cards. An existing binder must survive the upgrade with its layout reading "unset" (the
// 0 sentinel) rather than being handed a fabricated 3×3, and with no manual arrangement.
TEST(DatabaseTest, UpgradesAnExistingV11DatabaseAddingLayoutBlanksAndPlacements) {
    Database db(":memory:");
    // The v11 shape by hand (see the note on kSchemaVersion): a v1 card_binder plus the
    // region join table v11 added, holding one binder.
    db.exec(
        "CREATE TABLE card_binder(id TEXT PRIMARY KEY, name TEXT NOT NULL, region TEXT,"
        " inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.exec(
        "CREATE TABLE card_binder_region(binder_id TEXT NOT NULL"
        " REFERENCES card_binder(id) ON DELETE CASCADE, region TEXT NOT NULL,"
        " PRIMARY KEY (binder_id, region));");
    // card_copy has existed since v1 and v14's placement table keys into it, so the
    // fixture has to stand it up: with foreign_keys ON, ANY write that cascades into
    // card_binder_placement must resolve that parent, and an absent one fails the whole
    // statement with "no such table". Only the id column matters here.
    db.exec(
        "CREATE TABLE card_copy(id TEXT PRIMARY KEY, binder_id TEXT"
        " REFERENCES card_binder(id) ON DELETE SET NULL);");
    db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto Journey',NULL,'2026-07-14T00:00:00Z','2026-07-14T00:00:00Z');");
    db.exec("INSERT INTO card_binder_region(binder_id,region) VALUES('b1','Kanto');");
    db.setUserVersion(11);

    db.migrate();
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);

    // The existing binder keeps its name and region, and its layout backfills to 0 = unset.
    Statement layout(db,
                     "SELECT name, capacity, pocket_rows, pocket_columns"
                     " FROM card_binder WHERE id = 'b1';");
    ASSERT_TRUE(layout.step());
    EXPECT_EQ(layout.columnText(0), "Kanto Journey");
    EXPECT_EQ(layout.columnInt(1), 0);
    EXPECT_EQ(layout.columnInt(2), 0);
    EXPECT_EQ(layout.columnInt(3), 0);

    // card_binder_blank exists with its three-part primary key: one run per anchor, so a
    // second row for the same anchor is rejected rather than silently doubling the gap.
    EXPECT_NO_THROW(
        db.exec("INSERT INTO card_binder_blank(binder_id,before_dex_num,before_copy_id,blanks)"
                " VALUES('b1',650,'',2);"));
    EXPECT_ANY_THROW(
        db.exec("INSERT INTO card_binder_blank(binder_id,before_dex_num,before_copy_id,blanks)"
                " VALUES('b1',650,'',1);"));
    // A different anchor in the same binder is a distinct run.
    EXPECT_NO_THROW(
        db.exec("INSERT INTO card_binder_blank(binder_id,before_dex_num,before_copy_id,blanks)"
                " VALUES('b1',0,'copy-7',1);"));

    // card_binder_placement exists, keyed one row per copy per binder: a second placement
    // for the same card overwrites nothing and is rejected outright (the app upserts).
    db.exec("INSERT INTO card_copy(id,binder_id) VALUES('c1','b1');");
    EXPECT_NO_THROW(db.exec(
        "INSERT INTO card_binder_placement(binder_id,card_copy_id,before_dex_num,"
        "before_copy_id,ordinal) VALUES('b1','c1',650,'',0);"));
    EXPECT_ANY_THROW(db.exec(
        "INSERT INTO card_binder_placement(binder_id,card_copy_id,before_dex_num,"
        "before_copy_id,ordinal) VALUES('b1','c1',151,'',0);"));
    // Its card_copy_id carries a real foreign key, so a placement can't name a card that
    // isn't stored — the difference from a blank's before_copy_id, which may be ''.
    EXPECT_ANY_THROW(db.exec(
        "INSERT INTO card_binder_placement(binder_id,card_copy_id,before_dex_num,"
        "before_copy_id,ordinal) VALUES('b1','ghost',650,'',0);"));

    // Removing the binder cascades its blank AND placement rows away, as it does its
    // region rows.
    db.exec("DELETE FROM card_binder WHERE id = 'b1';");
    Statement orphans(db,
                      "SELECT (SELECT COUNT(*) FROM card_binder_blank)"
                      " + (SELECT COUNT(*) FROM card_binder_placement);");
    ASSERT_TRUE(orphans.step());
    EXPECT_EQ(orphans.columnInt(0), 0);
}

// Exercising the tables through DML proves they exist with the expected columns
// — a stronger contract than name-matching against sqlite_master.
TEST(DatabaseTest, TablesAcceptRowsAfterMigration) {
    Database db(":memory:");
    db.migrate();
    EXPECT_NO_THROW(db.exec(
        "INSERT INTO card_binder(id,name,region,inserted_at,updated_at)"
        " VALUES('b1','Kanto Journey','Kanto','2026-07-14T00:00:00Z','2026-07-14T00:00:00Z');"));
    EXPECT_NO_THROW(db.exec(
        "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
        "ownership,condition,binder_id,comments,inserted_at,updated_at)"
        " VALUES('c1',25,'MEW','EN','151/165','Owned','NearMint','b1','',"
        "'2026-07-14T00:00:00Z','2026-07-14T00:00:00Z');"));
    EXPECT_NO_THROW(db.exec(
        "INSERT INTO wishlist(pokemon_dex_num,inserted_at,updated_at)"
        " VALUES(1,'2026-07-14T00:00:00Z','2026-07-14T00:00:00Z');"));
    EXPECT_NO_THROW(db.exec(
        "INSERT INTO wishlist_source(pokemon_dex_num,source) VALUES(1,'https://example.test');"));
}

// transaction() commits the body's writes on success.
TEST(DatabaseTest, TransactionCommitsOnSuccess) {
    Database db(":memory:");
    db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY);");
    db.transaction([&] {
        db.exec("INSERT INTO t(id) VALUES(1);");
        db.exec("INSERT INTO t(id) VALUES(2);");
    });
    Statement count(db, "SELECT COUNT(*) FROM t;");
    ASSERT_TRUE(count.step());
    EXPECT_EQ(count.columnInt(0), 2);
}

// A throwing body rolls the whole transaction back (no partial write) and re-throws.
TEST(DatabaseTest, TransactionRollsBackAndRethrowsOnFailure) {
    Database db(":memory:");
    db.exec("CREATE TABLE t(id INTEGER PRIMARY KEY);");
    EXPECT_THROW(db.transaction([&] {
        db.exec("INSERT INTO t(id) VALUES(1);");
        throw std::runtime_error("boom");  // abandon mid-transaction
    }),
                 std::runtime_error);
    // The first insert must NOT have committed, and the connection must be usable
    // (the transaction was rolled back, not left open).
    Statement count(db, "SELECT COUNT(*) FROM t;");
    ASSERT_TRUE(count.step());
    EXPECT_EQ(count.columnInt(0), 0);
    EXPECT_NO_THROW(db.exec("INSERT INTO t(id) VALUES(9);"));
}

// A card_copy pointing at a missing binder must be rejected — this confirms both
// that foreign_keys=ON is applied and that the FK relationship exists.
TEST(DatabaseTest, ForeignKeysAreEnforced) {
    Database db(":memory:");
    db.migrate();
    EXPECT_THROW(
        db.exec("INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
                "ownership,condition,binder_id,comments,inserted_at,updated_at)"
                " VALUES('c2',25,'MEW','EN','151/165','Owned','NearMint','ghost','',"
                "'2026-07-14T00:00:00Z','2026-07-14T00:00:00Z');"),
        StorageError);
}

// backupTo writes a readable, independent copy — and crucially preserves
// user_version. A snapshot that lost the stamp would look like a fresh v0 file and
// be silently re-migrated from scratch when restored, which is the one failure mode
// that would make the whole backup feature worse than useless.
TEST(DatabaseTest, BackupToWritesAReadableCopyOfTheDatabase) {
    TempDir dir;
    Database db(":memory:");
    db.migrate();
    db.exec(
        "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
        "ownership,condition,binder_id,comments,inserted_at,updated_at)"
        " VALUES('c1',25,'BS','EN','58/102','Owned','NearMint',NULL,'hello',"
        "'2026-07-16T00:00:00Z','2026-07-16T00:00:00Z');");

    const fs::path snapshot = dir.path() / "snap.db";
    ASSERT_NO_THROW(db.backupTo(snapshot));
    ASSERT_TRUE(fs::exists(snapshot));

    Database restored(snapshot);
    EXPECT_EQ(restored.userVersion(), Database::kSchemaVersion);
    Statement stmt(restored, "SELECT comments FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "hello");
}

// The source connection stays fully usable after a snapshot — VACUUM INTO reads,
// it does not detach or lock the live database.
TEST(DatabaseTest, BackupToLeavesTheSourceDatabaseWritable) {
    TempDir dir;
    Database db(":memory:");
    db.migrate();
    db.backupTo(dir.path() / "snap.db");
    EXPECT_NO_THROW(
        db.exec("INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,"
                "ref_collector,ownership,condition,binder_id,comments,inserted_at,updated_at)"
                " VALUES('after',1,'BS','EN','1/102','Owned','NearMint',NULL,'',"
                "'2026-07-16T00:00:00Z','2026-07-16T00:00:00Z');"));
}

// SQLite refuses to overwrite an existing output file. The backup service leans on
// this as its "never clobber an existing backup" guard, so pin it — and pin that the
// existing file is left untouched rather than truncated on the way to failing.
TEST(DatabaseTest, BackupToRefusesAnExistingDestination) {
    TempDir dir;
    const fs::path taken = dir.path() / "taken.db";
    {
        std::ofstream out(taken);
        out << "not a database";
    }

    Database db(":memory:");
    db.migrate();
    EXPECT_THROW(db.backupTo(taken), StorageError);

    std::ifstream in(taken);
    std::string contents;
    std::getline(in, contents);
    EXPECT_EQ(contents, "not a database");
}

// A destination whose parent directory does not exist fails loudly rather than
// silently writing nothing.
TEST(DatabaseTest, BackupToRejectsAnUnwritableDestination) {
    TempDir dir;
    Database db(":memory:");
    db.migrate();
    EXPECT_THROW(db.backupTo(dir.path() / "no_such_dir" / "snap.db"), StorageError);
}

}  // namespace
