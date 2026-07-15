// test_env.h sets _DARWIN_C_SOURCE / _DEFAULT_SOURCE for setenv; include it
// before any system header so those declarations are visible under -std=c++23.
#include "../support/test_env.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <optional>

#include "core/storage/database.h"  // StorageError
#include "core/storage/workspace.h"

namespace {

namespace fs = std::filesystem;
using pokedex::configDir;
using pokedex::configFilePath;
using pokedex::readConfiguredWorkspacePath;
using pokedex::StorageError;
using pokedex::writeConfiguredWorkspacePath;
using pokedex_test::ScopedEnv;
using pokedex_test::TempDir;

TEST(WorkspaceTest, ConfigDirHonorsExplicitOverride) {
    TempDir tmp;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", tmp.path().string());
    EXPECT_EQ(configDir(), tmp.path());
    EXPECT_EQ(configFilePath(), tmp.path() / "config");
}

TEST(WorkspaceTest, ConfigDirFallsBackToXdgConfigHome) {
    TempDir tmp;
    ScopedEnv noOverride("POKEDEX_TCG_CONFIG_DIR", std::nullopt);
    ScopedEnv xdg("XDG_CONFIG_HOME", tmp.path().string());
    EXPECT_EQ(configDir(), tmp.path() / "pokedex-tcg");
}

TEST(WorkspaceTest, ConfiguredPathIsNulloptWhenNoConfigFile) {
    TempDir tmp;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", tmp.path().string());
    EXPECT_FALSE(readConfiguredWorkspacePath().has_value());
}

TEST(WorkspaceTest, WriteThenReadRoundTripsWorkspacePath) {
    TempDir cfgDir;
    TempDir wsDir;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());
    writeConfiguredWorkspacePath(wsDir.path());
    const std::optional<fs::path> read = readConfiguredWorkspacePath();
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(*read, wsDir.path());
}

TEST(WorkspaceTest, WriteThrowsWhenConfigDirCannotBeCreated) {
    TempDir tmp;
    // A regular file sits where the config dir's parent would need to be a
    // directory, so the config dir and file cannot be created.
    const fs::path blocker = tmp.path() / "blocker";
    { std::ofstream(blocker) << "x"; }
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", (blocker / "cfg").string());
    EXPECT_THROW(writeConfiguredWorkspacePath(tmp.path()), StorageError);
}

}  // namespace
