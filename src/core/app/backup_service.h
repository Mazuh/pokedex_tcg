#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/domain/types.h"
#include "core/storage/workspace.h"

namespace pokedex {

class Database;

// APP — raised when a backup cannot be written: an unusable destination, a failed
// database snapshot, or a failed archive entry.
class BackupError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The config key naming the user's chosen backup folder. Unlike the GUI-owned
// kDefaultLanguageConfigKey / kAssistantApiKeyConfigKey, this one lives in core: the
// pre-migration hook reads it at startup with no GUI in the picture.
inline constexpr char kBackupFolderConfigKey[] = "backup_folder";

// The two kinds of backup the app writes.
//   Data — just the SQLite database, as a plain .db file. Small, fast, and directly
//          usable with the sqlite3 CLI; this is also what the pre-migration hook writes.
//   Full — one self-contained .zip holding the database snapshot plus every cached
//          image, so a collection can be restored to a new folder wholesale.
enum class BackupKind { Data, Full };

// One backup found on disk. `takenAt` is read from the FILE NAME, never the file's
// mtime: a name survives being copied to a NAS, synced through iCloud, or restored
// from another machine's backup, and an mtime does not.
struct BackupRecord {
    BackupKind kind;
    std::filesystem::path file;
    Timestamp takenAt;
};

// --- Naming (pure) ---------------------------------------------------------
// "pokedex-data-20260813-091200.db" / "pokedex-full-20260813-091200.zip". The stamp
// is UTC at second precision, formatted from timestampToIso so there is exactly one
// time encoding in the app. `sequence` > 1 appends "-N", which is how two backups
// taken in the same second stay distinguishable (the parser ignores the suffix).
std::string backupFileName(BackupKind kind, Timestamp when, int sequence = 1);

// The inverse. Returns nullopt for anything that is not one of our backups — an
// unrelated file, a half-written *.partial, or a name whose extension does not match
// its kind. Being strict here is what stops a partial write from ever being reported
// as "last backup".
std::optional<BackupRecord> parseBackupFileName(const std::filesystem::path& file);

// --- Folder resolution -----------------------------------------------------
// <workspace-parent>/.PokedexTCG-backups — deliberately a SIBLING of the workspace,
// never a child: a full backup archives the whole workspace, so a nested destination
// would make it archive its own output.
std::filesystem::path defaultBackupFolder(const Workspace& ws);

// The configured `backup_folder`, falling back to defaultBackupFolder(ws). The
// fallback is what guarantees the automatic pre-migration backup always has somewhere
// to go, even for a user who has never opened Settings.
std::filesystem::path resolveBackupFolder(const Workspace& ws);

// Throws BackupError when `folder` is the workspace root, lies inside it, or exists
// as a regular file; otherwise creates it (and its parents) if absent. The single
// home of the recursion guard — called by both writers and by Settings on save, so a
// bad folder is rejected when it is chosen rather than when a backup fails.
void ensureUsableBackupFolder(const Workspace& ws, const std::filesystem::path& folder);

// --- Listing ---------------------------------------------------------------
// Non-recursive scan of `folder`, keeping only names parseBackupFileName accepts. An
// absent or unreadable folder yields an empty list rather than throwing: "where are
// my backups" is a read-only question that must never break the Settings screen.
std::vector<BackupRecord> listBackups(const std::filesystem::path& folder);

// When a backup of `kind` was last taken in `folder`, or nullopt if never.
std::optional<Timestamp> lastBackupAt(const std::filesystem::path& folder, BackupKind kind);

// --- Writers ---------------------------------------------------------------
// Both take a LIVE connection rather than a path. Both mechanisms need an open
// connection anyway, and a path-taking overload would quietly open a SECOND
// connection to the database the running app already holds — the very thing
// settings_view.cpp warns about. Both real callers have one in hand.
//
// Both return the file written, and both write to a "*.partial" temporary that is
// renamed into place only on success, so an interrupted backup never leaves a file
// that listBackups would trust.
std::filesystem::path writeDataBackup(Database& db, const Workspace& ws,
                                      const std::filesystem::path& folder, Timestamp when);
std::filesystem::path writeFullBackup(Database& db, const Workspace& ws,
                                      const std::filesystem::path& folder, Timestamp when);

// --- Archive readers -------------------------------------------------------
// Public because miniz is PRIVATE to pokedex_core and the test suite links only
// pokedex_core: without these, "a full backup is actually restorable" — the single
// most important claim this feature makes — could not be tested at all. They are also
// the natural foundation for a future "Restore…" button.
std::vector<std::string> archiveEntryNames(const std::filesystem::path& zipFile);

// Extract every entry into `destDir`, which must be empty or absent. Rejects absolute
// or "..'-bearing entry names (a zip-slip guard: this writes files from an archive
// that may not be one we produced).
void extractBackupTo(const std::filesystem::path& zipFile, const std::filesystem::path& destDir);

// --- The pre-migration hook ------------------------------------------------
// Where the hook's progress lines go. Injectable so tests can assert on them.
using BackupLogFn = std::function<void(const std::string&)>;

// The default sink: one "pokedex: <line>" per call on stdout, flushed. Flushing
// matters — if the app dies mid-migration, the line naming the backup's location is
// exactly the one the user needs.
BackupLogFn stdoutLog();

// True when a data backup is owed before migrating: an EXISTING database behind the
// current schema. A brand-new file (userVersion 0) is not — the v1 schema and its
// version stamp land in the same transaction, so no v0 file ever holds tables.
bool needsPreMigrationBackup(int userVersion);

// Back up (when owed), then migrate. THE chokepoint openWorkspace uses, so a schema
// upgrade can never rewrite a collection without a safety copy first.
//
// If the configured folder is unusable it falls back to defaultBackupFolder and logs
// why. If that fails too it throws, ABORTING the migration: a launch that fails is
// recoverable, a collection migrated without a backup may not be. The message names
// the config file and line, because this runs before the window exists and Settings
// cannot be reached to fix the folder.
void migrateWithBackup(Database& db, const Workspace& ws,
                       Timestamp now = std::chrono::system_clock::now(),
                       BackupLogFn log = stdoutLog());

// APP — the GUI-facing facade. Owns the live connection and workspace and supplies
// the clock, so a view can call runFullBackup() with no arguments, matching how every
// other section receives a service. The free functions above stay clock-free per the
// house rule that core never reads the clock; this is the one place that does.
class BackupService {
public:
    using Clock = std::function<Timestamp()>;
    static Clock systemClock();

    BackupService(Database& db, Workspace ws, Clock clock = systemClock());

    // The effective destination, configured or default.
    std::filesystem::path folder() const;

    std::filesystem::path runDataBackup();
    std::filesystem::path runFullBackup();
    std::optional<Timestamp> lastRun(BackupKind kind) const;

private:
    Database& db_;
    Workspace ws_;
    Clock clock_;
};

}  // namespace pokedex
