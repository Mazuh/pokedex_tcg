#include <gtest/gtest.h>

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

// The v1 → v2 upgrade of an existing database adds card_copy.ref_set_name without
// losing data, so a user's pre-v2 file keeps working.
TEST(DatabaseTest, UpgradesAnExistingV1DatabaseByAddingSetName) {
    Database db(":memory:");
    // Stand up a v1 card_copy (no ref_set_name) with a row, and stamp it v1.
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
    EXPECT_EQ(db.userVersion(), 2);
    // The pre-existing row survives and gains a blank set name.
    Statement stmt(db, "SELECT ref_set_name FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "");
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
