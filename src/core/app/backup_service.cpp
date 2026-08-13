#include "core/app/backup_service.h"

#include <miniz.h>

#include <algorithm>
#include <cctype>
#include <iostream>
#include <system_error>
#include <utility>

#include "core/storage/codecs.h"
#include "core/storage/database.h"

namespace pokedex {

namespace fs = std::filesystem;

namespace {

constexpr const char* kDataPrefix = "pokedex-data-";
constexpr const char* kFullPrefix = "pokedex-full-";
constexpr const char* kDataExtension = ".db";
constexpr const char* kFullExtension = ".zip";
constexpr const char* kPartialSuffix = ".partial";
constexpr const char* kDefaultFolderName = ".PokedexTCG-backups";

const char* prefixFor(BackupKind kind) {
    return kind == BackupKind::Data ? kDataPrefix : kFullPrefix;
}

const char* extensionFor(BackupKind kind) {
    return kind == BackupKind::Data ? kDataExtension : kFullExtension;
}

bool allDigits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) {
        return std::isdigit(c) != 0;
    });
}

// "20260813-091200" -> the Timestamp it names. Rebuilds the ISO-8601 form and defers
// to the storage codec rather than doing calendar arithmetic here: there is exactly
// one place in the app that converts between text and a Timestamp, and this is not it.
//
// The field ranges ARE checked here, though, and must be: timestampFromIso ends in
// timegm, which NORMALIZES out-of-range fields rather than rejecting them (month 13
// silently becomes January of the next year). Without this, a stray file named
// "pokedex-data-20261399-000000.db" would parse to a date far in the future and pin
// itself as "Last data backup" in Settings forever.
std::optional<Timestamp> parseStamp(const std::string& stamp) {
    if (stamp.size() != 15 || stamp[8] != '-') {
        return std::nullopt;
    }
    const std::string date = stamp.substr(0, 8);
    const std::string time = stamp.substr(9, 6);
    if (!allDigits(date) || !allDigits(time)) {
        return std::nullopt;
    }
    const auto field = [](const std::string& text, std::size_t at, std::size_t count) {
        return std::stoi(text.substr(at, count));
    };
    const int month = field(date, 4, 2);
    const int day = field(date, 6, 2);
    const int hour = field(time, 0, 2);
    const int minute = field(time, 2, 2);
    const int second = field(time, 4, 2);
    if (month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 ||
        second > 60) {  // 60 for a leap second
        return std::nullopt;
    }

    const std::string iso = date.substr(0, 4) + "-" + date.substr(4, 2) + "-" +
                            date.substr(6, 2) + "T" + time.substr(0, 2) + ":" +
                            time.substr(2, 2) + ":" + time.substr(4, 2) + "Z";
    try {
        return timestampFromIso(iso);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

// The inverse: an ISO-8601 UTC stamp squeezed into a filename-safe "YYYYMMDD-HHMMSS".
std::string formatStamp(Timestamp when) {
    const std::string iso = timestampToIso(when);  // YYYY-MM-DDThh:mm:ssZ
    return iso.substr(0, 4) + iso.substr(5, 2) + iso.substr(8, 2) + "-" + iso.substr(11, 2) +
           iso.substr(14, 2) + iso.substr(17, 2);
}

// Drop a trailing separator, so "/a/b/" and "/a/b" name the same directory and
// parent_path() means what it says (on "/a/b/" it would otherwise return "/a/b",
// turning the default backup folder into a CHILD of the workspace).
fs::path withoutTrailingSeparator(const fs::path& path) {
    return path.has_filename() ? path : path.parent_path();
}

// Folded lexically only — "." / ".." collapsed, symlinks left alone. Used to BUILD
// paths we show to the user: resolving links here would rewrite /tmp to /private/tmp,
// or an iCloud folder to its opaque backing store, and the user would not recognize
// the folder we claim to be writing to.
fs::path lexicalDir(const fs::path& path) {
    return withoutTrailingSeparator(path.lexically_normal());
}

// Fully resolved, symlinks and all. Used only to COMPARE paths, where following a
// link is a safety property: a backup folder that is a symlink INTO the workspace
// must still be caught by the recursion guard.
fs::path resolvedDir(const fs::path& path) {
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(path, ec);
    return withoutTrailingSeparator(ec ? path.lexically_normal() : resolved);
}

// Is `child` at or below `ancestor`? Compared COMPONENT-WISE, never as a string
// prefix: "/a/ws-backups" starts with "/a/ws" as text but is not inside it, and
// getting that wrong would refuse a perfectly good backup folder.
bool isAtOrInside(const fs::path& child, const fs::path& ancestor) {
    auto a = ancestor.begin();
    auto c = child.begin();
    for (; a != ancestor.end(); ++a, ++c) {
        if (c == child.end() || *c != *a) {
            return false;
        }
    }
    return true;
}

// Remove a leftover temporary from an earlier interrupted run. This deletes only
// files WE wrote and never finished (*.partial) — the app's standing promise is that
// it never deletes a real backup.
void discardStaleTemp(const fs::path& temp) {
    std::error_code ec;
    fs::remove(temp, ec);
}

// The first name in `folder` that is free, so two backups in the same second cannot
// collide. VACUUM INTO and the zip writer would both refuse an existing file anyway;
// this turns that into a second backup rather than an error.
fs::path freeBackupPath(const fs::path& folder, BackupKind kind, Timestamp when) {
    for (int sequence = 1; sequence < 1000; ++sequence) {
        const fs::path candidate = folder / backupFileName(kind, when, sequence);
        std::error_code ec;
        if (!fs::exists(candidate, ec) && !fs::exists(fs::path(candidate) += kPartialSuffix, ec)) {
            return candidate;
        }
    }
    throw BackupError("too many backups taken in the same second in " + folder.string());
}

// Snapshot the database to `destination`, restating any storage failure in terms of
// the operation the user asked for.
void snapshotInto(Database& db, const fs::path& destination) {
    try {
        db.backupTo(destination);
    } catch (const std::exception& e) {
        throw BackupError(std::string("could not snapshot the database: ") + e.what());
    }
}

// Closes the writer and clears both temporaries unless the archive was released —
// so an exception anywhere in the entry loop leaves no half-written .zip behind.
class ZipWriterGuard {
public:
    ZipWriterGuard(mz_zip_archive& zip, fs::path zipTemp, fs::path snapshot)
        : zip_(&zip), zipTemp_(std::move(zipTemp)), snapshot_(std::move(snapshot)) {}

    ZipWriterGuard(const ZipWriterGuard&) = delete;
    ZipWriterGuard& operator=(const ZipWriterGuard&) = delete;

    ~ZipWriterGuard() {
        if (!released_) {
            mz_zip_writer_end(zip_);
            discardStaleTemp(zipTemp_);
        }
        discardStaleTemp(snapshot_);  // the snapshot is temporary either way
    }

    void release() { released_ = true; }

private:
    mz_zip_archive* zip_;
    fs::path zipTemp_;
    fs::path snapshot_;
    bool released_ = false;
};

std::string zipError(mz_zip_archive& zip) {
    return mz_zip_get_error_string(mz_zip_get_last_error(&zip));
}

}  // namespace

// --- Naming ----------------------------------------------------------------

std::string backupFileName(BackupKind kind, Timestamp when, int sequence) {
    std::string name = std::string(prefixFor(kind)) + formatStamp(when);
    if (sequence > 1) {
        name += "-" + std::to_string(sequence);
    }
    return name + extensionFor(kind);
}

std::optional<BackupRecord> parseBackupFileName(const fs::path& file) {
    const std::string name = file.filename().string();
    for (const BackupKind kind : {BackupKind::Data, BackupKind::Full}) {
        const std::string prefix = prefixFor(kind);
        const std::string extension = extensionFor(kind);
        if (name.size() <= prefix.size() + extension.size() ||
            name.compare(0, prefix.size(), prefix) != 0 ||
            name.compare(name.size() - extension.size(), extension.size(), extension) != 0) {
            continue;
        }
        // Whatever sits between the prefix and the extension: the stamp, plus an
        // optional "-N" collision suffix that carries no time information.
        std::string middle =
            name.substr(prefix.size(), name.size() - prefix.size() - extension.size());
        if (middle.size() > 15) {
            const std::string suffix = middle.substr(15);
            if (suffix.size() < 2 || suffix[0] != '-' || !allDigits(suffix.substr(1))) {
                return std::nullopt;
            }
            middle = middle.substr(0, 15);
        }
        if (const std::optional<Timestamp> when = parseStamp(middle)) {
            return BackupRecord{kind, file, *when};
        }
        return std::nullopt;
    }
    return std::nullopt;
}

// --- Folder resolution -----------------------------------------------------

fs::path defaultBackupFolder(const Workspace& ws) {
    return lexicalDir(ws.root()).parent_path() / kDefaultFolderName;
}

fs::path resolveBackupFolder(const Workspace& ws) {
    if (const std::optional<std::string> configured = readConfigValue(kBackupFolderConfigKey)) {
        return fs::path(*configured);
    }
    return defaultBackupFolder(ws);
}

void ensureUsableBackupFolder(const Workspace& ws, const fs::path& folder) {
    if (folder.empty()) {
        throw BackupError("no backup folder was given");
    }
    const fs::path root = resolvedDir(ws.root());
    const fs::path destination = resolvedDir(folder);

    if (destination == root) {
        throw BackupError("the backup folder cannot be your workspace folder: " + folder.string());
    }
    if (isAtOrInside(destination, root)) {
        throw BackupError(
            "the backup folder cannot be inside your workspace — a full backup would "
            "archive its own output: " +
            folder.string());
    }

    std::error_code ec;
    if (fs::exists(destination, ec) && !fs::is_directory(destination, ec)) {
        throw BackupError("the backup folder is a file, not a folder: " + folder.string());
    }
    fs::create_directories(destination, ec);
    if (ec) {
        throw BackupError("could not create the backup folder (" + ec.message() +
                          "): " + folder.string());
    }
}

// --- Listing ---------------------------------------------------------------

std::vector<BackupRecord> listBackups(const fs::path& folder) {
    std::vector<BackupRecord> found;
    std::error_code ec;
    fs::directory_iterator it(folder, ec);
    const fs::directory_iterator end;
    if (ec) {
        return found;  // absent or unreadable: no backups to report, not an error
    }
    // Stepped with the error_code overload, never a range-for: operator++ is the
    // THROWING one, and this function is documented as never throwing. It is called
    // from the SettingsView constructor and from showEvent, so an exception escaping a
    // vanished network volume mid-scan would take down the window rather than a label.
    while (it != end) {
        if (it->is_regular_file(ec) && !ec) {
            if (std::optional<BackupRecord> record = parseBackupFileName(it->path())) {
                found.push_back(*record);
            }
        }
        it.increment(ec);
        if (ec) {
            break;  // report what we did find rather than nothing
        }
    }
    return found;
}

std::optional<Timestamp> lastBackupAt(const fs::path& folder, BackupKind kind) {
    std::optional<Timestamp> latest;
    for (const BackupRecord& record : listBackups(folder)) {
        if (record.kind == kind && (!latest || record.takenAt > *latest)) {
            latest = record.takenAt;
        }
    }
    return latest;
}

// --- Writers ---------------------------------------------------------------

fs::path writeDataBackup(Database& db, const Workspace& ws, const fs::path& folder,
                         Timestamp when) {
    ensureUsableBackupFolder(ws, folder);

    const fs::path final = freeBackupPath(folder, BackupKind::Data, when);
    fs::path temp = final;
    temp += kPartialSuffix;
    discardStaleTemp(temp);

    snapshotInto(db, temp);

    std::error_code ec;
    fs::rename(temp, final, ec);
    if (ec) {
        discardStaleTemp(temp);
        throw BackupError("could not finish writing the backup (" + ec.message() +
                          "): " + final.string());
    }
    return final;
}

fs::path writeFullBackup(Database& db, const Workspace& ws, const fs::path& folder,
                         Timestamp when) {
    ensureUsableBackupFolder(ws, folder);

    const fs::path final = freeBackupPath(folder, BackupKind::Full, when);
    fs::path zipTemp = final;
    zipTemp += kPartialSuffix;
    fs::path snapshot = final;
    snapshot.replace_extension(".db.partial");
    discardStaleTemp(zipTemp);
    discardStaleTemp(snapshot);

    // The database goes in as a consistent VACUUM INTO snapshot, never as a byte copy
    // of the live file: pages can change mid-read while the app holds it open.
    snapshotInto(db, snapshot);

    mz_zip_archive zip{};
    if (!mz_zip_writer_init_file(&zip, zipTemp.string().c_str(), 0)) {
        discardStaleTemp(snapshot);
        throw BackupError("could not create the backup archive (" + zipError(zip) +
                          "): " + final.string());
    }
    ZipWriterGuard guard(zip, zipTemp, snapshot);

    // Entry names mirror the workspace root, so recovery is literally "unzip into an
    // empty folder and point Settings at it".
    if (!mz_zip_writer_add_file(&zip, "pokedex.db", snapshot.string().c_str(), nullptr, 0,
                                MZ_DEFAULT_LEVEL)) {
        throw BackupError("could not add the database to the backup (" + zipError(zip) + ")");
    }

    // Walk the media cache. Every failure here is FATAL to the archive rather than
    // skipped: a "full" backup that quietly dropped the images would report success and
    // only be found wanting at restore time, which is the worst moment to learn it.
    // (Hence no skip_permission_denied either — an unreadable subtree must be heard.)
    std::error_code ec;
    if (fs::exists(ws.mediaDir(), ec)) {
        fs::recursive_directory_iterator media(ws.mediaDir(), ec);
        const fs::recursive_directory_iterator end;
        if (ec) {
            throw BackupError("could not read the media folder (" + ec.message() +
                              "): " + ws.mediaDir().string());
        }
        while (media != end) {
            const fs::path path = media->path();
            const bool regular = media->is_regular_file(ec);
            if (ec) {
                throw BackupError("could not read " + path.string() + " for the backup (" +
                                  ec.message() + ")");
            }
            if (regular) {
                // lexically_relative, not fs::relative: it is pure string work that cannot
                // fail, where the filesystem overload could quietly yield "" and collapse
                // every entry onto the name "media/". generic_string() because the ZIP
                // format mandates '/' separators.
                const std::string name =
                    "media/" + path.lexically_relative(ws.mediaDir()).generic_string();
                // Level 1: the media cache is PNG/JPEG, already compressed, so a higher
                // level burns CPU for nothing — and this runs on the UI thread.
                if (!mz_zip_writer_add_file(&zip, name.c_str(), path.string().c_str(), nullptr, 0,
                                            MZ_BEST_SPEED)) {
                    throw BackupError("could not add " + path.string() + " to the backup (" +
                                      zipError(zip) + ")");
                }
            }
            media.increment(ec);
            if (ec) {
                throw BackupError("could not read the media folder (" + ec.message() +
                                  "): " + ws.mediaDir().string());
            }
        }
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        throw BackupError("could not finish the backup archive (" + zipError(zip) + ")");
    }
    if (!mz_zip_writer_end(&zip)) {
        throw BackupError("could not close the backup archive (" + zipError(zip) + ")");
    }
    guard.release();
    discardStaleTemp(snapshot);

    fs::rename(zipTemp, final, ec);
    if (ec) {
        discardStaleTemp(zipTemp);
        throw BackupError("could not finish writing the backup (" + ec.message() +
                          "): " + final.string());
    }
    return final;
}

// --- Archive readers -------------------------------------------------------

std::vector<std::string> archiveEntryNames(const fs::path& zipFile) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipFile.string().c_str(), 0)) {
        throw BackupError("could not open the backup archive: " + zipFile.string());
    }
    std::vector<std::string> names;
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            mz_zip_reader_end(&zip);
            throw BackupError("could not read the backup archive: " + zipFile.string());
        }
        names.emplace_back(stat.m_filename);
    }
    mz_zip_reader_end(&zip);
    return names;
}

void extractBackupTo(const fs::path& zipFile, const fs::path& destDir) {
    std::error_code ec;
    if (fs::exists(destDir, ec) && !fs::is_empty(destDir, ec)) {
        throw BackupError("choose an empty or new folder to restore into: " + destDir.string());
    }
    fs::create_directories(destDir, ec);
    if (ec) {
        throw BackupError("could not create the restore folder (" + ec.message() +
                          "): " + destDir.string());
    }

    mz_zip_archive zip{};
    if (!mz_zip_reader_init_file(&zip, zipFile.string().c_str(), 0)) {
        throw BackupError("could not open the backup archive: " + zipFile.string());
    }
    const mz_uint count = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) {
            mz_zip_reader_end(&zip);
            throw BackupError("could not read the backup archive: " + zipFile.string());
        }
        if (mz_zip_reader_is_file_a_directory(&zip, i)) {
            continue;
        }
        // Zip-slip guard: an entry name must stay under destDir. We write files from an
        // archive that may not be one we produced, so absolute paths and ".." are refused
        // rather than trusted.
        const fs::path relative = fs::path(std::string(stat.m_filename));
        if (relative.is_absolute() ||
            std::any_of(relative.begin(), relative.end(),
                        [](const fs::path& part) { return part == ".."; })) {
            mz_zip_reader_end(&zip);
            throw BackupError("the backup archive contains an unsafe path: " +
                              std::string(stat.m_filename));
        }
        const fs::path target = destDir / relative;
        fs::create_directories(target.parent_path(), ec);
        if (!mz_zip_reader_extract_to_file(&zip, i, target.string().c_str(), 0)) {
            const std::string message = zipError(zip);
            mz_zip_reader_end(&zip);
            throw BackupError("could not restore " + std::string(stat.m_filename) + " (" +
                              message + ")");
        }
    }
    mz_zip_reader_end(&zip);
}

// --- The pre-migration hook ------------------------------------------------

BackupLogFn stdoutLog() {
    // std::endl, not '\n': if the app dies mid-migration, an unflushed buffer would
    // lose exactly the line telling the user where their backup went.
    return [](const std::string& line) { std::cout << "pokedex: " << line << std::endl; };
}

bool needsPreMigrationBackup(int userVersion) {
    return userVersion > 0 && userVersion < Database::kSchemaVersion;
}

void migrateWithBackup(Database& db, const Workspace& ws, Timestamp now, BackupLogFn log) {
    const int from = db.userVersion();
    if (!needsPreMigrationBackup(from)) {
        db.migrate();  // fresh database, or already current: nothing to protect
        return;
    }

    log("migrating database schema from v" + std::to_string(from) + " to v" +
        std::to_string(Database::kSchemaVersion));

    // The one remedy that always applies, on EVERY failure path. It has to be phrased to
    // fit both cases — a configured folder that went bad, and no configured folder at all
    // (the default for anyone who never opened Settings, and the case that reaches the
    // same-folder branch below) — because this aborts the launch before the window
    // exists, so Settings cannot be opened to fix it.
    const std::string remedy = " Set the `backup_folder=` line in " +
                               configFilePath().string() +
                               " to a writable folder (or remove it to use the default "
                               "beside your workspace), then reopen.";

    const fs::path configured = resolveBackupFolder(ws);
    fs::path written;
    try {
        written = writeDataBackup(db, ws, configured, now);
    } catch (const std::exception& e) {
        const fs::path fallback = defaultBackupFolder(ws);
        if (resolvedDir(configured) == resolvedDir(fallback)) {
            throw BackupError(std::string("could not write a backup before upgrading the "
                                          "database: ") +
                              e.what() + "." + remedy);
        }
        // The configured folder is unusable (unplugged NAS, revoked permissions). Fall
        // back to the default sibling rather than upgrading unprotected.
        log(std::string("could not back up to the configured folder (") + e.what() +
            "); falling back to " + fallback.string());
        try {
            written = writeDataBackup(db, ws, fallback, now);
        } catch (const std::exception& fallbackError) {
            throw BackupError(
                std::string("could not write a backup before upgrading the database: ") +
                fallbackError.what() + "." + remedy);
        }
    }

    log("wrote pre-migration backup to " + written.string());
    db.migrate();
}

// --- The facade ------------------------------------------------------------

BackupService::Clock BackupService::systemClock() {
    return [] { return std::chrono::system_clock::now(); };
}

BackupService::BackupService(Database& db, Workspace ws, Clock clock)
    : db_(db), ws_(std::move(ws)), clock_(std::move(clock)) {}

fs::path BackupService::folder() const { return resolveBackupFolder(ws_); }

fs::path BackupService::runDataBackup() {
    return writeDataBackup(db_, ws_, folder(), clock_());
}

fs::path BackupService::runFullBackup() {
    return writeFullBackup(db_, ws_, folder(), clock_());
}

std::optional<Timestamp> BackupService::lastRun(BackupKind kind) const {
    return lastBackupAt(folder(), kind);
}

}  // namespace pokedex
