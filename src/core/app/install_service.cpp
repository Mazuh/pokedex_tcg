#include "core/app/install_service.h"

#include <filesystem>

#include "core/app/backup_service.h"
#include "core/storage/database.h"
#include "core/storage/workspace.h"

namespace pokedex {

namespace fs = std::filesystem;

namespace {

// A directory counts as empty even if it holds only macOS Finder cruft, so a
// folder the user just made in Finder isn't wrongly rejected.
bool isEmptyOrAbsent(const fs::path& root) {
    std::error_code ec;
    if (!fs::exists(root, ec)) {
        return true;  // will be created fresh
    }
    if (!fs::is_directory(root, ec)) {
        return false;  // a regular file at that path is not a usable workspace
    }
    fs::directory_iterator it(root, ec);
    if (ec) {
        // The folder exists but we cannot read it (permissions, transient I/O).
        // Surface the real problem rather than silently treating it as empty.
        throw WorkspaceError("could not read the chosen folder (" + ec.message() +
                             "): " + root.string());
    }
    for (const auto& entry : it) {
        const std::string name = entry.path().filename().string();
        if (name == ".DS_Store" || name == ".localized") {
            continue;
        }
        return false;
    }
    return true;
}

}  // namespace

Workspace openWorkspace(const fs::path& root) {
    Workspace ws(root);
    // Creating the media dir also creates the workspace root (its parent), so
    // pokedex.db's directory is guaranteed to exist before we open it.
    fs::create_directories(ws.mediaDir());
    Database db(ws.dbPath());
    // Not a bare migrate(): this is the app's schema-upgrade chokepoint, and an
    // upgrade must never rewrite an existing collection without writing a data
    // backup first. A fresh database has nothing to protect, so this is a no-op on
    // first run.
    migrateWithBackup(db, ws);
    return ws;
}

Workspace initWorkspace(const fs::path& root) {
    if (!isEmptyOrAbsent(root)) {
        throw WorkspaceError(
            "Choose an empty or new folder for the workspace; this one already "
            "has files in it: " +
            root.string());
    }
    Workspace ws = openWorkspace(root);
    writeConfiguredWorkspacePath(ws.root());
    return ws;
}

std::optional<Workspace> configuredWorkspace() {
    std::optional<fs::path> path = readConfiguredWorkspacePath();
    if (!path) {
        return std::nullopt;
    }
    return Workspace(*path);
}

}  // namespace pokedex
