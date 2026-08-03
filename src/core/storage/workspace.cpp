#include "core/storage/workspace.h"

#include <cstdlib>
#include <fstream>
#include <map>
#include <string>

#include "core/storage/database.h"  // StorageError

namespace pokedex {

namespace fs = std::filesystem;

namespace {

// The reserved key under which the workspace path is stored in the config file.
constexpr const char* kWorkspaceKey = "workspace";

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

// Parse the config file into a key→value map. The format is one `key=value` per line;
// blank lines are skipped. For backward compatibility with the pre-key-value format
// (which stored just the bare workspace path on the first line), a first line without
// an '=' is taken as the workspace path. std::map keeps writes deterministic (sorted).
std::map<std::string, std::string> loadConfigMap() {
    std::map<std::string, std::string> config;
    const fs::path file = configFilePath();
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return config;
    }
    std::ifstream in(file);
    if (!in) {
        return config;
    }
    std::string line;
    bool firstLine = true;
    while (std::getline(in, line)) {
        const std::string trimmed = trim(line);
        if (!trimmed.empty()) {
            const auto eq = trimmed.find('=');
            if (eq == std::string::npos) {
                // Legacy bare-path line: only the very first line meant anything.
                if (firstLine) {
                    config[kWorkspaceKey] = trimmed;
                }
            } else {
                config[trim(trimmed.substr(0, eq))] = trim(trimmed.substr(eq + 1));
            }
        }
        firstLine = false;
    }
    return config;
}

// Write the whole key→value map back to the config file, creating the config dir as
// needed. Throws StorageError if the write/flush fails (the caller relies on the file
// actually landing — e.g. losing the workspace path locks the user out of their folder).
void writeConfigMap(const std::map<std::string, std::string>& config) {
    const fs::path file = configFilePath();
    std::error_code ec;
    fs::create_directories(configDir(), ec);

    std::ofstream out(file, std::ios::trunc);
    for (const auto& [key, value] : config) {
        out << key << '=' << value << '\n';
    }
    out.close();  // flush; sets failbit if the write/flush failed
    if (!out) {
        throw StorageError("could not write config file: " + file.string());
    }
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
    if (auto value = readConfigValue(kWorkspaceKey)) {
        return fs::path(*value);
    }
    return std::nullopt;
}

void writeConfiguredWorkspacePath(const fs::path& workspaceRoot) {
    // Merge the path into the existing config so a workspace change never drops other
    // settings (e.g. the default language). If we cannot record the path, the user
    // would be sent back to first-run and locked out of their now-populated folder by
    // the emptiness guard, so writeConfigMap fails loudly.
    writeConfigValue(kWorkspaceKey, workspaceRoot.string());
}

std::optional<std::string> readConfigValue(const std::string& key) {
    const std::map<std::string, std::string> config = loadConfigMap();
    const auto it = config.find(key);
    if (it == config.end() || it->second.empty()) {
        return std::nullopt;
    }
    return it->second;
}

void writeConfigValue(const std::string& key, const std::string& value) {
    std::map<std::string, std::string> config = loadConfigMap();
    if (value.empty()) {
        config.erase(key);  // an empty value clears the setting rather than storing blank
    } else {
        config[key] = value;
    }
    writeConfigMap(config);
}

}  // namespace pokedex
