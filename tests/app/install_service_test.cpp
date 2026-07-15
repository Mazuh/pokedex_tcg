// test_env.h sets _DARWIN_C_SOURCE / _DEFAULT_SOURCE for setenv; include it
// before any system header so those declarations are visible under -std=c++23.
#include "../support/test_env.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "core/app/install_service.h"
#include "core/storage/workspace.h"

namespace {

namespace fs = std::filesystem;
using pokedex::configuredWorkspace;
using pokedex::initWorkspace;
using pokedex::openWorkspace;
using pokedex::Workspace;
using pokedex::WorkspaceError;
using pokedex_test::ScopedEnv;
using pokedex_test::TempDir;

TEST(InstallServiceTest, InitCreatesDbMediaAndConfig) {
    TempDir cfgDir;
    TempDir parent;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());
    const fs::path root = parent.path() / "workspace";

    const Workspace ws = initWorkspace(root);

    EXPECT_EQ(ws.root(), root);
    EXPECT_TRUE(fs::exists(ws.dbPath()));
    EXPECT_TRUE(fs::is_directory(ws.mediaDir()));
    EXPECT_TRUE(fs::exists(cfgDir.path() / "config"));
}

TEST(InstallServiceTest, InitRejectsAPopulatedFolder) {
    TempDir cfgDir;
    TempDir existing;  // a non-empty folder — stands in for e.g. the user's home
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());
    { std::ofstream(existing.path() / "my_photos.txt") << "not ours\n"; }

    EXPECT_THROW(initWorkspace(existing.path()), WorkspaceError);
    // The guard fires before anything is created: no db, no config file written.
    EXPECT_FALSE(fs::exists(existing.path() / "pokedex.db"));
    EXPECT_FALSE(fs::exists(cfgDir.path() / "config"));
}

TEST(InstallServiceTest, InitAcceptsAnExistingEmptyFolder) {
    TempDir cfgDir;
    TempDir empty;  // exists but empty — a folder the user pre-created is fine
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());

    EXPECT_NO_THROW(initWorkspace(empty.path()));
    EXPECT_TRUE(fs::exists(empty.path() / "pokedex.db"));
}

TEST(InstallServiceTest, OpenWorkspaceReopensAnExistingWorkspace) {
    TempDir cfgDir;
    TempDir parent;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());
    const fs::path root = parent.path() / "workspace";

    initWorkspace(root);
    // The relaunch path reopens the (now populated) workspace without the guard.
    EXPECT_NO_THROW(openWorkspace(root));
}

TEST(InstallServiceTest, ConfiguredWorkspaceReadsBackTheInitializedPath) {
    TempDir cfgDir;
    TempDir parent;
    ScopedEnv cfg("POKEDEX_TCG_CONFIG_DIR", cfgDir.path().string());

    EXPECT_FALSE(configuredWorkspace().has_value());  // first run: no config file

    const fs::path root = parent.path() / "workspace";
    initWorkspace(root);

    const std::optional<Workspace> ws = configuredWorkspace();
    ASSERT_TRUE(ws.has_value());
    EXPECT_EQ(ws->root(), root);
}

}  // namespace
