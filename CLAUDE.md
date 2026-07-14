# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

Pokédex TCG — a macOS desktop app (currently at the Hello World bootstrap
stage) for managing a physical Pokémon card collection using local files.
See `README.md` for the product vision and glossary.

## Tech stack

- **Language:** C++23 (Apple Clang)
- **GUI:** Qt 6 Widgets
- **Build:** CMake + Ninja
- **Tests:** GoogleTest (fetched via CMake `FetchContent`) run through CTest
- **CI:** GitHub Actions on `macos-latest` (`.github/workflows/ci.yml`)

## Layout

```
src/
  main.cpp        GUI entry point (Qt Widgets)
  greeting.{h,cpp} Qt-free core logic
tests/
  test_greeting.cpp GoogleTest unit tests
CMakeLists.txt    single top-level build file
```

**Architecture rule:** keep non-GUI logic in the Qt-free `pokedex_core`
library so it stays unit-testable headlessly. The GUI layer (`pokedex_tcg`)
depends on `pokedex_core` and Qt; tests depend only on `pokedex_core`. Do not
pull Qt into the core library or into tests.

## Common commands

```sh
# Configure (once, or after changing CMakeLists.txt)
cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

# Build
cmake --build build

# Run the app
./build/pokedex_tcg

# Run tests
ctest --test-dir build --output-on-failure
```

## Conventions

- C++23, no compiler extensions (`CMAKE_CXX_EXTENSIONS OFF`).
- Core logic goes in `namespace pokedex`.
- Add new tests to `tests/` and register the source in `CMakeLists.txt`;
  they are auto-discovered by CTest via `gtest_discover_tests`.
- After nontrivial changes, build and run the tests before considering the
  work done.
