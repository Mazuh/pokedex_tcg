#pragma once

#include <filesystem>
#include <optional>
#include <stdexcept>

#include "core/storage/workspace.h"

namespace pokedex {

// APP — raised when a chosen workspace folder cannot be used (e.g. it is a
// non-empty directory that is not already a Pokedex TCG workspace).
class WorkspaceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// APP — orchestrates first-run setup and workspace opening. This is the logic
// behind the GUI wizard's "create workspace" action, kept Qt-free so it stays
// unit-testable and reusable by a headless/CLI caller.

// Create a fresh workspace rooted at `root`: make the directory and its media/
// cache, create + migrate pokedex.db, then record the path in the config file.
//
// Guards against picking a populated folder (e.g. the user's home): `root` must
// be non-existent or an empty directory. Otherwise a WorkspaceError is thrown
// before anything is created — this never deletes or overwrites existing files.
// (Re-opening an already-created workspace is openWorkspace's job, not this.)
Workspace initWorkspace(const std::filesystem::path& root);

// Open (and migrate) an existing workspace without touching the config file.
// Permissive: it ensures the workspace directories and database exist, so it is
// safe to call on a workspace that is already set up (this is the relaunch path).
Workspace openWorkspace(const std::filesystem::path& root);

// The workspace named by the config file, or nullopt on first run (config file
// absent) — the caller then shows the setup wizard.
std::optional<Workspace> configuredWorkspace();

}  // namespace pokedex
