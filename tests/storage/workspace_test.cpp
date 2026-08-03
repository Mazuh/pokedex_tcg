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
using pokedex::readConfigValue;
using pokedex::readConfiguredWorkspacePath;
using pokedex::StorageError;
using pokedex::writeConfigValue;
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

TEST(WorkspaceTest, ConfigValueRoundTripsAndClearsOnEmpty) {
    TempDir cfgDir;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());

    EXPECT_FALSE(readConfigValue("default_language").has_value());
    writeConfigValue("default_language", "FR");
    ASSERT_TRUE(readConfigValue("default_language").has_value());
    EXPECT_EQ(*readConfigValue("default_language"), "FR");

    // Writing an empty value clears the key rather than storing a blank.
    writeConfigValue("default_language", "");
    EXPECT_FALSE(readConfigValue("default_language").has_value());
}

TEST(WorkspaceTest, WorkspaceAndConfigValueDoNotClobberEachOther) {
    TempDir cfgDir;
    TempDir wsDir;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());

    // Each setting is written through its own call; neither drops the other.
    writeConfiguredWorkspacePath(wsDir.path());
    writeConfigValue("default_language", "DE");

    const std::optional<fs::path> ws = readConfiguredWorkspacePath();
    ASSERT_TRUE(ws.has_value());
    EXPECT_EQ(*ws, wsDir.path());
    ASSERT_TRUE(readConfigValue("default_language").has_value());
    EXPECT_EQ(*readConfigValue("default_language"), "DE");

    // Re-writing the workspace preserves the language, and vice versa.
    TempDir wsDir2;
    writeConfiguredWorkspacePath(wsDir2.path());
    ASSERT_TRUE(readConfigValue("default_language").has_value());
    EXPECT_EQ(*readConfigValue("default_language"), "DE");
    EXPECT_EQ(*readConfiguredWorkspacePath(), wsDir2.path());
}

TEST(WorkspaceTest, ReadsLegacyBarePathConfigAndUpgradesOnWrite) {
    TempDir cfgDir;
    TempDir wsDir;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());

    // The pre-key-value format: just the bare workspace path on line 1.
    { std::ofstream(configFilePath()) << wsDir.path().string() << '\n'; }
    const std::optional<fs::path> ws = readConfiguredWorkspacePath();
    ASSERT_TRUE(ws.has_value());
    EXPECT_EQ(*ws, wsDir.path());

    // A subsequent write upgrades the file to key=value while keeping the workspace,
    // and the newly added setting reads back.
    writeConfigValue("default_language", "IT");
    EXPECT_EQ(*readConfiguredWorkspacePath(), wsDir.path());
    ASSERT_TRUE(readConfigValue("default_language").has_value());
    EXPECT_EQ(*readConfigValue("default_language"), "IT");
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
