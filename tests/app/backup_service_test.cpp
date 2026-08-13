// test_env.h sets _DARWIN_C_SOURCE / _DEFAULT_SOURCE for setenv; include it
// before any system header so those declarations are visible under -std=c++23.
#include "../support/test_env.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "core/app/backup_service.h"
#include "core/app/install_service.h"
#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"
#include "core/storage/workspace.h"

namespace {

namespace fs = std::filesystem;

using pokedex::BackupError;
using pokedex::BackupKind;
using pokedex::BackupRecord;
using pokedex::Database;
using pokedex::Statement;
using pokedex::Timestamp;
using pokedex::Workspace;
using pokedex_test::ScopedEnv;
using pokedex_test::TempDir;

Timestamp at(const std::string& iso) { return pokedex::timestampFromIso(iso); }

// A hermetic workspace: its own config dir (so readConfigValue/writeConfigValue never
// touch the real one) and a workspace folder under a temp parent, which is also where
// defaultBackupFolder() will want to write.
struct Fixture {
    TempDir configDir;
    TempDir parent;
    ScopedEnv configOverride{"POKEDEX_TCG_CONFIG_DIR", configDir.path().string()};
    fs::path root = parent.path() / "workspace";
    Workspace ws{root};

    Fixture() { pokedex::openWorkspace(root); }

    fs::path backupDir() const { return parent.path() / "backups"; }

    // Add one card copy through plain SQL — the point of these tests is the backup,
    // not the repository layer.
    static void insertCopy(Database& db, const std::string& id, const std::string& comment) {
        db.exec(
            "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
            "ownership,condition,binder_id,comments,inserted_at,updated_at)"
            " VALUES('" +
            id + "',25,'BS','EN','58/102','Owned','NearMint',NULL,'" + comment +
            "','2026-07-16T00:00:00Z','2026-07-16T00:00:00Z');");
    }

    static void writeFile(const fs::path& path, const std::string& contents) {
        fs::create_directories(path.parent_path());
        std::ofstream out(path, std::ios::binary);
        out << contents;
    }

    static std::string readFile(const fs::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
};

// --- Naming ----------------------------------------------------------------

TEST(BackupServiceTest, BackupFileNameRoundTripsForBothKinds) {
    const Timestamp when = at("2026-08-13T09:12:00Z");

    const std::string dataName = pokedex::backupFileName(BackupKind::Data, when);
    EXPECT_EQ(dataName, "pokedex-data-20260813-091200.db");
    const std::optional<BackupRecord> data = pokedex::parseBackupFileName(dataName);
    ASSERT_TRUE(data.has_value());
    EXPECT_EQ(data->kind, BackupKind::Data);
    EXPECT_EQ(data->takenAt, when);

    const std::string fullName = pokedex::backupFileName(BackupKind::Full, when);
    EXPECT_EQ(fullName, "pokedex-full-20260813-091200.zip");
    const std::optional<BackupRecord> full = pokedex::parseBackupFileName(fullName);
    ASSERT_TRUE(full.has_value());
    EXPECT_EQ(full->kind, BackupKind::Full);
    EXPECT_EQ(full->takenAt, when);
}

// The "-N" collision suffix keeps two same-second backups distinguishable, and carries
// no time information of its own.
TEST(BackupServiceTest, ASequenceSuffixIsNamedAndParsedBack) {
    const Timestamp when = at("2026-08-13T09:12:00Z");
    const std::string name = pokedex::backupFileName(BackupKind::Data, when, 2);
    EXPECT_EQ(name, "pokedex-data-20260813-091200-2.db");
    const std::optional<BackupRecord> parsed = pokedex::parseBackupFileName(name);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->takenAt, when);
}

// Strictness here is load-bearing: a half-written *.partial must never be mistaken for
// a finished backup and reported as "last backup" in Settings.
TEST(BackupServiceTest, ParseRejectsAnythingThatIsNotOneOfOurBackups) {
    const std::vector<std::string> rejected = {
        "notes.txt",
        "pokedex.db",
        "pokedex-data-20260813-091200.db.partial",
        "pokedex-full-20260813-091200.zip.partial",
        "pokedex-data-2026813-091200.db",   // short date
        "pokedex-data-20260813_091200.db",  // wrong separator
        "pokedex-data-abcdefgh-091200.db",  // non-numeric
        "pokedex-data-20260813-091200.zip",  // extension does not match the kind
        "pokedex-full-20260813-091200.db",
        "pokedex-data-.db",
        ".DS_Store",
        // Impossible field values. These must be rejected HERE: timestampFromIso ends in
        // timegm, which normalizes rather than rejects, so month 99 would otherwise parse
        // to a date far in the future and pin itself as "last backup" forever.
        "pokedex-data-20261399-000000.db",  // month 13, day 99
        "pokedex-data-20260899-091200.db",  // day 99
        "pokedex-data-20260813-991200.db",  // hour 99
        "pokedex-data-20260813-099900.db",  // minute 99
        "pokedex-data-20260813-091299.db",  // second 99
        "pokedex-data-20260013-091200.db",  // month 0
    };
    for (const std::string& name : rejected) {
        EXPECT_FALSE(pokedex::parseBackupFileName(name).has_value()) << name;
    }
}

// --- Folder resolution -----------------------------------------------------

TEST(BackupServiceTest, DefaultBackupFolderIsASiblingOfTheWorkspaceNotAChild) {
    const Workspace ws{fs::path("/tmp/collections/MyPokedex")};
    const fs::path folder = pokedex::defaultBackupFolder(ws);
    EXPECT_EQ(folder.filename(), ".PokedexTCG-backups");
    EXPECT_EQ(folder.parent_path(), fs::path("/tmp/collections"));
    // The whole point: a full backup archives the workspace, so the destination must
    // never sit inside it.
    EXPECT_FALSE(folder.string().starts_with(ws.root().string()));
}

// A trailing separator on the workspace path must not turn the sibling into a child.
TEST(BackupServiceTest, DefaultBackupFolderIgnoresATrailingSeparator) {
    const Workspace ws{fs::path("/tmp/collections/MyPokedex/")};
    EXPECT_EQ(pokedex::defaultBackupFolder(ws), fs::path("/tmp/collections/.PokedexTCG-backups"));
}

TEST(BackupServiceTest, ResolvePrefersTheConfiguredFolderOverTheDefault) {
    Fixture fx;
    EXPECT_EQ(pokedex::resolveBackupFolder(fx.ws), pokedex::defaultBackupFolder(fx.ws));

    pokedex::writeConfigValue(pokedex::kBackupFolderConfigKey, fx.backupDir().string());
    EXPECT_EQ(pokedex::resolveBackupFolder(fx.ws), fx.backupDir());

    // Clearing the key falls back again, so a user can return to the default.
    pokedex::writeConfigValue(pokedex::kBackupFolderConfigKey, "");
    EXPECT_EQ(pokedex::resolveBackupFolder(fx.ws), pokedex::defaultBackupFolder(fx.ws));
}

TEST(BackupServiceTest, NestedAndSelfDestinationsAreRefused) {
    Fixture fx;
    EXPECT_THROW(pokedex::ensureUsableBackupFolder(fx.ws, fx.root), BackupError);
    EXPECT_THROW(pokedex::ensureUsableBackupFolder(fx.ws, fx.root / "backups"), BackupError);
    EXPECT_THROW(pokedex::ensureUsableBackupFolder(fx.ws, fx.root / "media" / "deep"),
                 BackupError);
}

// A sibling whose name merely SHARES A PREFIX with the workspace is perfectly legal.
// This is the test that catches a string-prefix containment check (`rfind(root) == 0`)
// where a component-wise one is required.
TEST(BackupServiceTest, ASiblingSharingANamePrefixIsAccepted) {
    Fixture fx;
    const fs::path sibling = fs::path(fx.root.string() + "-backups");
    EXPECT_NO_THROW(pokedex::ensureUsableBackupFolder(fx.ws, sibling));
    EXPECT_TRUE(fs::is_directory(sibling));
}

// The guard resolves symlinks, so a backup folder that merely POINTS into the
// workspace is caught too — otherwise a full backup would still archive its own
// output, just via a link. (Path *construction* deliberately does not resolve links;
// the default folder must read back the way the user wrote it.)
TEST(BackupServiceTest, ASymlinkPointingIntoTheWorkspaceIsRefused) {
    Fixture fx;
    const fs::path link = fx.parent.path() / "sneaky-backups";
    std::error_code ec;
    fs::create_directory_symlink(fx.root / "backups", link, ec);
    if (ec) {
        GTEST_SKIP() << "symlinks unavailable: " << ec.message();
    }
    EXPECT_THROW(pokedex::ensureUsableBackupFolder(fx.ws, link), BackupError);
}

TEST(BackupServiceTest, TheBackupFolderIsCreatedWhenAbsent) {
    Fixture fx;
    const fs::path nested = fx.backupDir() / "deeper";
    ASSERT_FALSE(fs::exists(nested));
    EXPECT_NO_THROW(pokedex::ensureUsableBackupFolder(fx.ws, nested));
    EXPECT_TRUE(fs::is_directory(nested));
}

TEST(BackupServiceTest, AFileWhereTheBackupFolderShouldBeIsRefused) {
    Fixture fx;
    const fs::path asFile = fx.parent.path() / "not-a-folder";
    Fixture::writeFile(asFile, "x");
    EXPECT_THROW(pokedex::ensureUsableBackupFolder(fx.ws, asFile), BackupError);
}

// --- Data backup -----------------------------------------------------------

// The most important test in the feature: a data backup must be a fully readable
// database carrying the SAME schema version. A snapshot that lost user_version would
// look like a fresh file and be silently re-migrated from scratch on restore.
TEST(BackupServiceTest, ADataBackupIsAReadableDatabaseWithTheSameRowsAndSchemaVersion) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    Fixture::insertCopy(db, "c1", "first edition");

    const fs::path written =
        pokedex::writeDataBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));
    EXPECT_EQ(written.filename(), "pokedex-data-20260813-091200.db");
    ASSERT_TRUE(fs::exists(written));

    Database restored(written);
    EXPECT_EQ(restored.userVersion(), Database::kSchemaVersion);
    Statement stmt(restored, "SELECT comments FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "first edition");
}

TEST(BackupServiceTest, ADataBackupLeavesTheLiveDatabaseUsable) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    Fixture::insertCopy(db, "c1", "before");
    pokedex::writeDataBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));
    EXPECT_NO_THROW(Fixture::insertCopy(db, "c2", "after"));
}

// Two backups in the same second must both survive — the app never overwrites or
// deletes a backup, so the second gets a "-2" name rather than failing.
TEST(BackupServiceTest, WritingTwiceInTheSameSecondKeepsBothBackups) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    const Timestamp when = at("2026-08-13T09:12:00Z");

    const fs::path first = pokedex::writeDataBackup(db, fx.ws, fx.backupDir(), when);
    const fs::path second = pokedex::writeDataBackup(db, fx.ws, fx.backupDir(), when);
    EXPECT_NE(first, second);
    EXPECT_TRUE(fs::exists(first));
    EXPECT_TRUE(fs::exists(second));
    EXPECT_EQ(second.filename(), "pokedex-data-20260813-091200-2.db");
}

TEST(BackupServiceTest, ADataBackupLeavesNoTemporaryFilesBehind) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    pokedex::writeDataBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));

    for (const fs::directory_entry& entry : fs::directory_iterator(fx.backupDir())) {
        EXPECT_EQ(entry.path().extension(), ".db") << entry.path().string();
    }
}

// --- Full backup -----------------------------------------------------------

TEST(BackupServiceTest, AFullBackupContainsTheDatabaseAndEveryMediaFile) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    Fixture::insertCopy(db, "c1", "zipped");
    Fixture::writeFile(fx.ws.mediaDir() / "cards" / "c1.png", "card-bytes");
    Fixture::writeFile(fx.ws.mediaDir() / "pokemon" / "pikachu" / "official-artwork.png",
                       "art-bytes");

    const fs::path zip =
        pokedex::writeFullBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));
    EXPECT_EQ(zip.filename(), "pokedex-full-20260813-091200.zip");

    std::vector<std::string> names = pokedex::archiveEntryNames(zip);
    std::sort(names.begin(), names.end());
    const std::vector<std::string> expected = {
        "media/cards/c1.png",
        "media/pokemon/pikachu/official-artwork.png",
        "pokedex.db",
    };
    EXPECT_EQ(names, expected);
}

// The automated proof of the recovery procedure the UI and README document: unzip into
// an empty folder and you have a working workspace.
TEST(BackupServiceTest, AFullBackupExtractsToARestorableWorkspace) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    Fixture::insertCopy(db, "c1", "restore me");
    Fixture::writeFile(fx.ws.mediaDir() / "cards" / "c1.png", "card-bytes");

    const fs::path zip =
        pokedex::writeFullBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));

    const fs::path restoredRoot = fx.parent.path() / "restored";
    ASSERT_NO_THROW(pokedex::extractBackupTo(zip, restoredRoot));

    const Workspace restoredWs{restoredRoot};
    Database restored(restoredWs.dbPath());
    EXPECT_EQ(restored.userVersion(), Database::kSchemaVersion);
    Statement stmt(restored, "SELECT comments FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "restore me");
    EXPECT_EQ(Fixture::readFile(restoredWs.mediaDir() / "cards" / "c1.png"), "card-bytes");
}

TEST(BackupServiceTest, AFullBackupLeavesNoTemporaryFilesBehind) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    Fixture::writeFile(fx.ws.mediaDir() / "cards" / "c1.png", "card-bytes");
    pokedex::writeFullBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));

    std::vector<std::string> left;
    for (const fs::directory_entry& entry : fs::directory_iterator(fx.backupDir())) {
        left.push_back(entry.path().filename().string());
    }
    const std::vector<std::string> expected = {"pokedex-full-20260813-091200.zip"};
    EXPECT_EQ(left, expected);
}

TEST(BackupServiceTest, RestoringRefusesANonEmptyDestination) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    const fs::path zip =
        pokedex::writeFullBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));

    const fs::path occupied = fx.parent.path() / "occupied";
    Fixture::writeFile(occupied / "something.txt", "x");
    EXPECT_THROW(pokedex::extractBackupTo(zip, occupied), BackupError);
}

// A "full" backup that quietly shipped without the images would report success and only
// be found wanting at restore time. If the media folder cannot be read, fail loudly.
TEST(BackupServiceTest, AFullBackupFailsRatherThanSilentlyDroppingUnreadableMedia) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    Fixture::writeFile(fx.ws.mediaDir() / "cards" / "c1.png", "card-bytes");

    // Make the media folder unreadable. Skipped when running as root, which can read it
    // regardless (CI containers often do).
    std::error_code ec;
    fs::permissions(fx.ws.mediaDir(), fs::perms::none, ec);
    if (ec) {
        GTEST_SKIP() << "cannot change permissions: " << ec.message();
    }
    fs::directory_iterator probe(fx.ws.mediaDir(), ec);
    if (!ec) {
        fs::permissions(fx.ws.mediaDir(), fs::perms::owner_all, ec);
        GTEST_SKIP() << "media folder still readable (running as root?)";
    }

    EXPECT_THROW(pokedex::writeFullBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z")),
                 BackupError);
    fs::permissions(fx.ws.mediaDir(), fs::perms::owner_all, ec);

    // And it left nothing behind that could pass for a finished backup.
    EXPECT_TRUE(pokedex::listBackups(fx.backupDir()).empty());
}

// A workspace with no media folder at all is not an error — there is simply nothing to
// archive beside the database.
TEST(BackupServiceTest, AFullBackupSucceedsWhenThereIsNoMediaFolder) {
    Fixture fx;
    Database db(fx.ws.dbPath());
    db.migrate();
    fs::remove_all(fx.ws.mediaDir());

    const fs::path zip =
        pokedex::writeFullBackup(db, fx.ws, fx.backupDir(), at("2026-08-13T09:12:00Z"));
    const std::vector<std::string> expected = {"pokedex.db"};
    EXPECT_EQ(pokedex::archiveEntryNames(zip), expected);
}

// --- Listing / last run ----------------------------------------------------

TEST(BackupServiceTest, LastRunReadsTheNewestNameOfEachKindAndIgnoresJunk) {
    Fixture fx;
    const fs::path dir = fx.backupDir();
    fs::create_directories(dir);
    for (const std::string& name : {std::string("pokedex-data-20260101-000000.db"),
                                    std::string("pokedex-data-20260813-091200.db"),
                                    std::string("pokedex-full-20260601-101010.zip"),
                                    std::string("pokedex-data-20270101-000000.db.partial"),
                                    std::string("notes.txt"), std::string(".DS_Store")}) {
        Fixture::writeFile(dir / name, "x");
    }

    EXPECT_EQ(pokedex::listBackups(dir).size(), 3u);
    EXPECT_EQ(pokedex::lastBackupAt(dir, BackupKind::Data), at("2026-08-13T09:12:00Z"));
    EXPECT_EQ(pokedex::lastBackupAt(dir, BackupKind::Full), at("2026-06-01T10:10:10Z"));
}

TEST(BackupServiceTest, LastRunIsNulloptForAnAbsentOrEmptyFolder) {
    Fixture fx;
    EXPECT_FALSE(pokedex::lastBackupAt(fx.backupDir(), BackupKind::Data).has_value());
    fs::create_directories(fx.backupDir());
    EXPECT_FALSE(pokedex::lastBackupAt(fx.backupDir(), BackupKind::Full).has_value());
    EXPECT_TRUE(pokedex::listBackups(fx.backupDir()).empty());
}

// --- The pre-migration hook ------------------------------------------------

TEST(BackupServiceTest, NeedsPreMigrationBackupOnlyForAnExistingOlderDatabase) {
    EXPECT_FALSE(pokedex::needsPreMigrationBackup(0));  // fresh file: nothing to protect
    EXPECT_TRUE(pokedex::needsPreMigrationBackup(1));
    EXPECT_TRUE(pokedex::needsPreMigrationBackup(Database::kSchemaVersion - 1));
    EXPECT_FALSE(pokedex::needsPreMigrationBackup(Database::kSchemaVersion));
    EXPECT_FALSE(pokedex::needsPreMigrationBackup(Database::kSchemaVersion + 5));
}

// Stand up the v1 shape by hand. Per database.h, from v12 on a step ALTERs a v1 table,
// so "migrate a fresh DB then roll user_version back" is not a safe way to replay a
// step — and a v14 FOREIGN KEY cascades into card_binder_placement, whose card_copy
// parent must therefore exist.
void standUpV1Database(const fs::path& dbPath) {
    Database db(dbPath);
    db.exec(
        "CREATE TABLE card_copy(id TEXT PRIMARY KEY, pokemon_dex_num INTEGER NOT NULL,"
        " ref_expansion TEXT NOT NULL, ref_language TEXT NOT NULL, ref_collector TEXT NOT NULL,"
        " ownership TEXT NOT NULL, condition TEXT NOT NULL, binder_id TEXT,"
        " comments TEXT NOT NULL DEFAULT '', inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.exec(
        "INSERT INTO card_copy(id,pokemon_dex_num,ref_expansion,ref_language,ref_collector,"
        "ownership,condition,binder_id,comments,inserted_at,updated_at)"
        " VALUES('c1',6,'OBF','EN','125/197','Owned','NearMint',NULL,'precious',"
        "'2026-07-16T00:00:00Z','2026-07-16T00:00:00Z');");
    db.exec(
        "CREATE TABLE card_binder(id TEXT PRIMARY KEY, name TEXT NOT NULL, region TEXT,"
        " inserted_at TEXT NOT NULL, updated_at TEXT NOT NULL);");
    db.setUserVersion(1);
}

// Does the pre-migration schema still lack a column a later step adds? That is how we
// prove the backup captured the state BEFORE the upgrade, not after it.
bool hasColumn(Database& db, const std::string& table, const std::string& column) {
    Statement stmt(db, "SELECT COUNT(*) FROM pragma_table_info('" + table + "') WHERE name = '" +
                           column + "';");
    return stmt.step() && stmt.columnInt(0) > 0;
}

TEST(BackupServiceTest, APreMigrationBackupIsWrittenOnARealUpgrade) {
    Fixture fx;
    fs::remove(fx.ws.dbPath());
    standUpV1Database(fx.ws.dbPath());
    pokedex::writeConfigValue(pokedex::kBackupFolderConfigKey, fx.backupDir().string());

    std::vector<std::string> logged;
    Database db(fx.ws.dbPath());
    pokedex::migrateWithBackup(db, fx.ws, at("2026-08-13T09:12:00Z"),
                               [&logged](const std::string& line) { logged.push_back(line); });

    // The live database was upgraded...
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    EXPECT_TRUE(hasColumn(db, "card_copy", "ref_set_name"));

    // ...and a backup of its PRE-upgrade state sits beside it.
    const fs::path backup = fx.backupDir() / "pokedex-data-20260813-091200.db";
    ASSERT_TRUE(fs::exists(backup));
    Database saved(backup);
    EXPECT_EQ(saved.userVersion(), 1);
    EXPECT_FALSE(hasColumn(saved, "card_copy", "ref_set_name"));
    Statement stmt(saved, "SELECT comments FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "precious");

    ASSERT_EQ(logged.size(), 2u);
    EXPECT_EQ(logged[0], "migrating database schema from v1 to v" +
                             std::to_string(Database::kSchemaVersion));
    EXPECT_TRUE(logged[1].starts_with("wrote pre-migration backup to "));
    EXPECT_NE(logged[1].find("pokedex-data-20260813-091200.db"), std::string::npos);
}

// The ordering is the whole point of the feature: back up FIRST, migrate second. If a
// migration blows up, the safety copy must already be on disk.
TEST(BackupServiceTest, APreMigrationBackupSurvivesAFailedMigration) {
    Fixture fx;
    fs::remove(fx.ws.dbPath());
    standUpV1Database(fx.ws.dbPath());
    {
        // Sabotage the upgrade: a table the v6 step wants to create is already there
        // with a conflicting shape, so migrate() throws part-way through.
        Database db(fx.ws.dbPath());
        db.exec("CREATE TABLE card_set_cache(nonsense TEXT);");
    }
    pokedex::writeConfigValue(pokedex::kBackupFolderConfigKey, fx.backupDir().string());

    Database db(fx.ws.dbPath());
    EXPECT_THROW(pokedex::migrateWithBackup(db, fx.ws, at("2026-08-13T09:12:00Z"),
                                            [](const std::string&) {}),
                 std::exception);

    const fs::path backup = fx.backupDir() / "pokedex-data-20260813-091200.db";
    ASSERT_TRUE(fs::exists(backup));
    Database saved(backup);
    EXPECT_EQ(saved.userVersion(), 1);
    Statement stmt(saved, "SELECT comments FROM card_copy WHERE id = 'c1';");
    ASSERT_TRUE(stmt.step());
    EXPECT_EQ(stmt.columnText(0), "precious");
}

TEST(BackupServiceTest, NoPreMigrationBackupForAFreshOrCurrentWorkspace) {
    Fixture fx;  // its ctor already ran openWorkspace, i.e. a fresh v0 -> current migration
    std::vector<std::string> logged;

    Database db(fx.ws.dbPath());
    pokedex::migrateWithBackup(db, fx.ws, at("2026-08-13T09:12:00Z"),
                               [&logged](const std::string& line) { logged.push_back(line); });

    EXPECT_TRUE(logged.empty());
    EXPECT_TRUE(pokedex::listBackups(pokedex::defaultBackupFolder(fx.ws)).empty());
    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
}

// An unusable configured folder must not leave an upgrade unprotected: fall back to
// the default sibling, say so, and carry on.
TEST(BackupServiceTest, AnUnusableConfiguredFolderFallsBackToTheDefault) {
    Fixture fx;
    fs::remove(fx.ws.dbPath());
    standUpV1Database(fx.ws.dbPath());
    // Nested inside the workspace: rejected by the recursion guard.
    pokedex::writeConfigValue(pokedex::kBackupFolderConfigKey, (fx.root / "inside").string());

    std::vector<std::string> logged;
    Database db(fx.ws.dbPath());
    ASSERT_NO_THROW(pokedex::migrateWithBackup(
        db, fx.ws, at("2026-08-13T09:12:00Z"),
        [&logged](const std::string& line) { logged.push_back(line); }));

    EXPECT_EQ(db.userVersion(), Database::kSchemaVersion);
    const std::vector<BackupRecord> fallback =
        pokedex::listBackups(pokedex::defaultBackupFolder(fx.ws));
    ASSERT_EQ(fallback.size(), 1u);
    EXPECT_EQ(fallback.front().kind, BackupKind::Data);
    ASSERT_EQ(logged.size(), 3u);
    EXPECT_NE(logged[1].find("falling back to"), std::string::npos);
}

// When even the fallback cannot be written we abort rather than migrate unprotected —
// and the message must name the config file, since this happens before the window
// exists and Settings cannot be reached to fix the folder.
TEST(BackupServiceTest, AnUnwritableFallbackAbortsTheMigration) {
    Fixture fx;
    fs::remove(fx.ws.dbPath());
    standUpV1Database(fx.ws.dbPath());
    pokedex::writeConfigValue(pokedex::kBackupFolderConfigKey, (fx.root / "inside").string());
    // Block the default sibling by occupying its path with a regular file.
    Fixture::writeFile(pokedex::defaultBackupFolder(fx.ws), "in the way");

    Database db(fx.ws.dbPath());
    try {
        pokedex::migrateWithBackup(db, fx.ws, at("2026-08-13T09:12:00Z"),
                                   [](const std::string&) {});
        FAIL() << "expected the migration to abort";
    } catch (const BackupError& e) {
        const std::string message = e.what();
        EXPECT_NE(message.find("backup_folder="), std::string::npos) << message;
        EXPECT_NE(message.find(pokedex::configFilePath().string()), std::string::npos) << message;
    }

    // The collection was left untouched, so a fixed config can retry cleanly.
    EXPECT_EQ(db.userVersion(), 1);
}

// --- The facade ------------------------------------------------------------

TEST(BackupServiceTest, TheFacadeWritesToTheResolvedFolderAndReportsItsLastRun) {
    Fixture fx;
    pokedex::writeConfigValue(pokedex::kBackupFolderConfigKey, fx.backupDir().string());
    Database db(fx.ws.dbPath());
    db.migrate();

    const Timestamp when = at("2026-08-13T09:12:00Z");
    pokedex::BackupService service(db, fx.ws, [when] { return when; });
    EXPECT_EQ(service.folder(), fx.backupDir());
    EXPECT_FALSE(service.lastRun(BackupKind::Data).has_value());

    const fs::path written = service.runDataBackup();
    EXPECT_EQ(written.parent_path(), fx.backupDir());
    EXPECT_EQ(service.lastRun(BackupKind::Data), when);
    EXPECT_FALSE(service.lastRun(BackupKind::Full).has_value());

    service.runFullBackup();
    EXPECT_EQ(service.lastRun(BackupKind::Full), when);
}

}  // namespace
