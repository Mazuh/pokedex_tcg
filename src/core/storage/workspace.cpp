#include "core/storage/workspace.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include "core/storage/database.h"  // StorageError

namespace pokedex {

namespace fs = std::filesystem;

namespace {

// Return the value of environment variable `name`, or nullopt when it is unset
// or empty (an empty override is treated as "not set").
std::optional<std::string> env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

// Trim leading/trailing ASCII whitespace (incl. the trailing newline the config
// file is written with).
std::string trim(const std::string& s) {
    const auto notSpace = [](unsigned char c) { return c != ' ' && c != '\t' && c != '\r' && c != '\n'; };
    auto begin = s.begin();
    while (begin != s.end() && !notSpace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    auto end = s.end();
    while (end != begin && !notSpace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

}  // namespace

Workspace::Workspace(fs::path root) : root_(std::move(root)) {}

fs::path Workspace::dbPath() const { return root_ / "pokedex.db"; }

fs::path Workspace::mediaDir() const { return root_ / "media"; }

fs::path configDir() {
    if (auto explicitDir = env("POKEDEX_TCG_CONFIG_DIR")) {
        return fs::path(*explicitDir);
    }
    if (auto xdg = env("XDG_CONFIG_HOME")) {
        return fs::path(*xdg) / "pokedex-tcg";
    }
    // $HOME is always set for a logged-in user; fall back to "." only defensively.
    fs::path home = env("HOME").transform([](const std::string& h) { return fs::path(h); })
                        .value_or(fs::path("."));
    return home / ".config" / "pokedex-tcg";
}

fs::path configFilePath() { return configDir() / "config"; }

std::optional<fs::path> readConfiguredWorkspacePath() {
    const fs::path file = configFilePath();
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return std::nullopt;
    }
    std::ifstream in(file);
    if (!in) {
        return std::nullopt;
    }
    std::string line;
    std::getline(in, line);
    const std::string path = trim(line);
    if (path.empty()) {
        return std::nullopt;
    }
    return fs::path(path);
}

void writeConfiguredWorkspacePath(const fs::path& workspaceRoot) {
    const fs::path file = configFilePath();
    std::error_code ec;
    fs::create_directories(configDir(), ec);

    std::ofstream out(file, std::ios::trunc);
    out << workspaceRoot.string() << '\n';
    out.close();  // flush; sets failbit if the write/flush failed

    // If we cannot record the workspace path, the user would be sent back to
    // first-run on next launch and locked out of their now-populated folder by
    // the emptiness guard. Fail loudly instead so init as a whole fails.
    if (!out) {
        throw StorageError("could not write workspace config file: " + file.string());
    }
}

}  // namespace pokedex
