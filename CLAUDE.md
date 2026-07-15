# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

Pokédex TCG — a macOS desktop app (currently at the Hello World bootstrap
stage) for managing a physical Pokémon card collection using local files.
See `README.md` for the product vision and glossary.

## Tech stack

- **Language:** C++23 (Apple Clang)
- **GUI:** Qt 6 Widgets
- **Storage:** SQLite — the **sole** persistence format (system libsqlite3 via
  `find_package(SQLite3)`, linked PRIVATE into `pokedex_core`; the C API is used
  directly, never Qt SQL, so core stays Qt-free). No CSV/JSON: they are not a
  storage format and not an export target — out of scope.
- **Build:** CMake + Ninja
- **Tests:** GoogleTest (fetched via CMake `FetchContent`) run through CTest
- **CI:** GitHub Actions matrix on `macos-latest` (Apple Clang) and
  `ubuntu-latest` (GCC, `qt6-base-dev`) — a second-compiler / portability
  gate, built with `-Wall -Wextra -Werror` via `-DPOKEDEX_WERROR=ON`
  (`.github/workflows/ci.yml`)

## Layout

```
src/
  core/                 pokedex_core — Qt-free, unit-tested; layered:
    domain/             entities & value objects, pure logic — three zones:
                          catalog (Region, Pokemon); collection (CardBinder,
                          CardCopy, Wishlist, CardReference, CardCondition,
                          CardOwnership); inferred (CollectionStatus,
                          CardBinderEntry) — see "Domain model" below
    storage/            local-file persistence (SQLite DB + media cache),
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
shape, not a scaffold to pre-create. `domain/` is populated. `storage/` holds
workspace resolution, the SQLite `Database` wrapper + schema/migrations, the
`codecs` (region/ownership/condition enums ↔ tokens, timestamps ↔ ISO-8601),
and repositories for `CardBinder`, `CardCopy`, and `Wishlist` (the copy/wishlist
repos are read-mostly for now — an `add` primitive plus the reads the binder
guide and Pokédex browser need, e.g. `CardCopyRepository::ownedCountsByDexNum`;
the full write surface lands with copy management). `app/` holds
`install_service`, `BinderService` (binder CRUD verbs), `BinderGuideService`
(the `buildBinderEntries` the inferred zone refers to), and
`PokemonBrowseService` (`listAll` → every catalog species paired with its
owned-copy count, the unscoped Pokédex browser's data). `gui/views/` holds the
`MainWindow` shell (a macOS-style sidebar selecting sections in an outer
`QStackedWidget`), the first-run setup dialog, the binders section
(`BindersPage`, a table with its own list ⇄ binder-guide stack), the new-binder
editor, the binder guide view, and the Pokémon browser (`PokemonListView`).
`gui/models/` is still empty — no Qt item models yet; views adapt core →
widgets inline. (The `greeting.{h,cpp}` bootstrap placeholders were removed once
real domain types existed.)

**Architecture rule:** keep non-GUI logic in the Qt-free `pokedex_core`
library so it stays unit-testable headlessly. The GUI layer (`pokedex_tcg`)
depends on `pokedex_core` and Qt; tests depend only on `pokedex_core`. Do not
pull Qt into the core library or into tests.

**Layering rule (within core):** `domain/` depends on nothing; `storage/`
and `app/` depend on `domain/`; `app/` may use `storage/`. `domain/` must
never include from `storage/` or `app/`. Enforced by review — `pokedex_core`
is a single library, layered by folder rather than separate targets.

## Domain model

The domain splits into three zones (each type's reasoning lives in its
docstring):

- **Catalog** — authoritative-but-fixed reference data, shipped as
  compile-time constants/enums with no persistence: `Region`, `Pokemon`.
- **Collection** — the user's mutable source of truth, the only data that is
  ever stored: `CardBinder`, `CardCopy`, `Wishlist` (plus the `CardReference`
  value object and the `CardCondition` / `CardOwnership` enums). Each root
  carries flat `insertedAt` / `updatedAt` UTC audit stamps.
- **Inferred** — pure functions of the source of truth, never stored and
  never mutated, only recomputed: `CollectionStatus` (precedence-ordered) and
  the `CardBinderEntry` projection.

Conventions that hold across the model:

- **References are by id/value, never by pointer.** A `CardCopy` holds a
  `CardBinderId` and a `pokemonDexNum`, not a `CardBinder&` — so every entity
  is independently constructible, testable, and later serializable.
- **Domain code never reads the system clock.** Any current time is obtained
  in `app/` and passed in; the UTC audit stamps are set by the caller, not by
  the domain. This keeps domain logic deterministic and unit-testable (tests
  pass fixed timestamps).
- **Behavior lives in the `app/` service layer.** Domain types are plain data
  with value semantics — no use-case logic hangs off them. The operations
  that orchestrate the model (buying a card, removing a copy, building a
  binder's entries, resolving a Pokémon's `CollectionStatus`) are use-case
  services in `app/` that read and write domain entities. Put plainly:
  `domain/` types are the nouns, `app/` services are the verbs.
- **Naming.** Prefix domain types so they don't collide with technical terms
  (`CardCondition`, `CardBinder`, `CardOwnership` rather than bare
  `Condition` / `Binder` / `Ownership`), and name numeric fields explicitly
  (`pokemonDexNum`, not `pokemon`).
- **No `Card` entity for now.** A `CardCopy` records its printing
  (`CardReference`) and species (`pokemonDexNum`) directly. A full card
  catalog is a future fetch-and-cache concern, not something this domain
  stores or manages.
- **Docs live as docstrings**, next to the code they describe, rather than as
  standalone markdown design notes.

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
- Always ask before installing or adding a new dependency — a library, a CMake
  `FetchContent` entry, or a system package. Do not add one unprompted.
- Add new tests under `tests/` mirroring `src/core` (e.g. `tests/domain/`)
  and register the source in `CMakeLists.txt`; they are auto-discovered by
  CTest via `gtest_discover_tests`.
- Prefer tests that pin real contracts (construction/field round-trips,
  documented orderings like the `CollectionStatus` precedence and
  `CardCondition` best-to-worst) over change-detector tests such as asserting
  an enum's value count.
- After nontrivial changes, build and run the tests before considering the
  work done.

**GUI navigation.** The app is a macOS-style shell: `MainWindow` has a left
sidebar (a `QListWidget` source list, Finder/Settings-style) selecting sections
in an outer `QStackedWidget`. Prefer navigating *within* the window — swap pages
in a `QStackedWidget` (as `BindersPage` does: binder table ⇄ binder guide, with
a Back button) — over opening a second top-level window. A separate window or
modal dialog is a deliberate, rare exception (the first-run setup and the
new-binder form are two), not the default for showing more detail. Display
strings stay out of Qt-free core: a GUI-side helper maps enums to labels
(`region_labels.h`, `status_labels.h`), kept separate from the storage tokens.

**Long lists paginate by infinite scroll, not Prev/Next.** The default for a
long, scannable list is incremental loading (`PokemonListView`): render a chunk,
append the next as the user scrolls near the bottom (drive it off
`verticalScrollBar()`'s `valueChanged` for scroll-to-bottom plus `rangeChanged`
with a `maximum()==0` guard so a short first chunk still fills the viewport).
Append rows — never rebuild what's on screen. Reach for explicit paging controls
only when infinite scroll genuinely doesn't fit.

**Storage writes that span statements go in a transaction.** A repository `add`
that writes more than one row (e.g. `Wishlist` — a parent row plus its source
rows) must wrap them in `BEGIN` / `COMMIT` with a best-effort `ROLLBACK` on
failure, mirroring `Database::migrate()`. Otherwise a mid-sequence failure
leaves a committed partial row that a primary key then blocks re-adding.

## Workflow after commits and big changes

This is process guidance, followed best-effort — not a hard gate. Apply it
after every commit, and also after any substantial change even without a
commit ("big change" is a judgment call).

**Definition of done.** Saying the work is "done" asserts that the CI checks
and the code review below actually ran and are green. If any step was skipped,
say so explicitly ("done, but did not run X because Y") — never let a bare
"done" imply a step that did not happen.

- **CI checks run before each commit or amend.** Locally reproduce the CI
  gate — configure/build with `-DPOKEDEX_WERROR=ON` (`-Wall -Wextra -Werror`)
  and run `ctest` — and only commit/amend when it is green. Never commit over
  a red build or failing tests.
- **Independent code review, then amend.** After committing, run the
  `code-review` skill on that commit — it spawns child reviewers with fresh,
  focused context (the same effect as reviewing from a separate session, minus
  authoring bias), and returns a consolidated report. Apply the accepted
  findings and fold them into the commit with `git commit --amend`. Committing
  *first* and amending *after* is deliberate: if a fix goes wrong mid-flow,
  `git reflog` can recover the pre-amend state. (Reminder: local commits only —
  never push, per the never-push memory.)
- **Revisit the docs after big chunks of change.** Consider whether `CLAUDE.md`
  and `README.md` need updating. Update `CLAUDE.md` especially in light of
  patterns observed during the work — recurring problems, footguns, or new
  features/conventions worth capturing so the next session inherits them.
