#pragma once

#include <filesystem>
#include <optional>

namespace pokedex {

// STORAGE — a resolved collection workspace: the directory the user chose (which
// may live in iCloud / Dropbox / a NAS) holding the SQLite database and the
// media cache. This is a value object; constructing one touches no filesystem —
// creating the directories is the install service's job.
class Workspace {
public:
    explicit Workspace(std::filesystem::path root);

    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path dbPath() const;    // <root>/pokedex.db
    std::filesystem::path mediaDir() const;  // <root>/media

private:
    std::filesystem::path root_;
};

// The app's private config directory. It is an internal implementation detail —
// never surfaced to the user — whose only job is to remember where the workspace
// lives. Resolution order (first match wins):
//   1. $POKEDEX_TCG_CONFIG_DIR              (explicit override; used by tests)
//   2. $XDG_CONFIG_HOME/pokedex-tcg
//   3. $HOME/.config/pokedex-tcg            (the default)
std::filesystem::path configDir();

// The config file recording the active workspace path: <configDir>/config.
std::filesystem::path configFilePath();

// The workspace path recorded in the config file, or nullopt when the file is
// absent/empty — i.e. this is a first run and the setup wizard should be shown.
std::optional<std::filesystem::path> readConfiguredWorkspacePath();

// Record `workspaceRoot` in the config file, creating the config dir as needed.
void writeConfiguredWorkspacePath(const std::filesystem::path& workspaceRoot);

}  // namespace pokedex
