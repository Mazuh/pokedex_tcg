#include <gtest/gtest.h>

#include <stdexcept>

#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace {

using pokedex::Database;
using pokedex::Statement;
using pokedex::StorageError;

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
    db.setUserVersion(2);

    db.migrate();
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    // The pre-existing row survives, keeps its set name, and gains a blank card name.
    Statement stmt(db, "SELECT ref_set_name, ref_name FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "Obsidian Flames");
    EXPECT_EQ(stmt.columnText(1), "");
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

}  // namespace
