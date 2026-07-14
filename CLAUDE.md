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
  core/                 pokedex_core — Qt-free, unit-tested; layered:
    domain/             entities & value objects, pure logic (Pokémon,
                          Region, Card, CardReference, CardCopy, Condition,
                          Binder, Wishlist)
    storage/            local-file persistence (CSV/JSON + media cache),
                          workspace-directory resolution, repositories
    app/                use-case services orchestrating domain + storage
  gui/                  pokedex_tcg — Qt Widgets only; depends on core
    main.cpp            GUI entry point
    views/              one widget/window per screen
    models/             Qt item models adapting core → widgets
tests/                  mirrors src/core (domain/ storage/ app/)
data/                   seed National Pokédex reference catalog shipped
                          with the app (distinct from the user's workspace)
docs/                   design notes / decisions (optional)
CMakeLists.txt          single top-level build file
```

Headers are **co-located** with their `.cpp` (no separate `include/`). The
include root is `src/`, so includes read namespaced:
`#include "core/domain/pokemon.h"`, `#include "gui/views/..."`.

Folders are created as real code lands; the tree above is the target
shape, not a scaffold to pre-create. `greeting.{h,cpp}` are bootstrap
placeholders, removed once the first real domain type exists.

**Architecture rule:** keep non-GUI logic in the Qt-free `pokedex_core`
library so it stays unit-testable headlessly. The GUI layer (`pokedex_tcg`)
depends on `pokedex_core` and Qt; tests depend only on `pokedex_core`. Do not
pull Qt into the core library or into tests.

**Layering rule (within core):** `domain/` depends on nothing; `storage/`
and `app/` depend on `domain/`; `app/` may use `storage/`. `domain/` must
never include from `storage/` or `app/`. Enforced by review — `pokedex_core`
is a single library, layered by folder rather than separate targets.

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
