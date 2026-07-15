#pragma once

// setenv/unsetenv are POSIX; request their declarations so this compiles under
// strict -std=c++23 on both libc++ (macOS) and glibc (Linux). Must precede any
// system header — see the note at the top of the tests that include this file.
#ifndef _DARWIN_C_SOURCE
#define _DARWIN_C_SOURCE 1
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>

namespace pokedex_test {

// Creates a unique temporary directory and removes it recursively on
// destruction. Keeps storage tests hermetic — no real workspace or ~/.config is
// touched.
class TempDir {
public:
    TempDir() {
        static std::atomic<unsigned> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const std::string name = "pokedex_test_" + std::to_string(stamp) + "_" +
                                 std::to_string(counter.fetch_add(1));
        path_ = std::filesystem::temp_directory_path() / name;
        std::filesystem::create_directories(path_);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

// Sets (or, when value == nullopt, unsets) an environment variable for the
// object's lifetime, restoring the previous state on destruction so env
// mutations don't leak between tests.
class ScopedEnv {
public:
    ScopedEnv(const char* name, std::optional<std::string> value) : name_(name) {
        if (const char* prev = std::getenv(name)) {
            had_ = true;
            previous_ = prev;
        }
        if (value) {
            ::setenv(name, value->c_str(), 1);
        } else {
            ::unsetenv(name);
        }
    }
    ~ScopedEnv() {
        if (had_) {
            ::setenv(name_.c_str(), previous_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    bool had_ = false;
    std::string previous_;
};

}  // namespace pokedex_test
