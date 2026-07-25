# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

Pokédex TCG — a macOS desktop app (currently at the Hello World bootstrap
stage) for managing a physical Pokémon card collection using local files.
See `README.md` for the product vision; the domain glossary now lives in the
`src/core/domain/` docstrings (each term is defined next to the type it names).

## Tech stack

- **Language:** C++23 (Apple Clang)
- **GUI:** Qt 6 Widgets
- **Storage:** SQLite — the **sole** persistence format (system libsqlite3 via
  `find_package(SQLite3)`, linked PRIVATE into `pokedex_core`; the C API is used
  directly, never Qt SQL, so core stays Qt-free). No CSV/JSON *as a storage
  format* or export target — out of scope.
- **JSON (parsing only):** nlohmann/json (fetched via CMake `FetchContent`,
  `JSON_SystemInclude ON`, linked PRIVATE into `pokedex_core`) — used **only** to
  parse the external card-catalog (pokemontcg.io) API responses in
  `core/app/card_catalog_parse`. It never appears in a public core header (the
  parsers take `std::string`, return plain structs) and is never a storage format.
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
shape, not a scaffold to pre-create. `domain/` is populated. Schema migrations are
**incremental and additive**: `Database::migrate()` applies each step whose target
version exceeds the file's `user_version` (v1 = initial schema; v2 added
`card_copy.ref_set_name`; v3 added `card_copy.ref_name`, the printed card name),
so a fresh DB runs the whole chain and an existing one
only the tail — bump `kSchemaVersion` and add a step (never edit `kSchemaV1`) when
the schema changes. `storage/` holds
workspace resolution, the SQLite `Database` wrapper + schema/migrations, the
`codecs` (region/ownership/condition enums ↔ tokens, timestamps ↔ ISO-8601),
and repositories for `CardBinder`, `CardCopy`, and `Wishlist`. The `CardCopy`
repo now carries the full CRUD surface — `add`, `find`, `listAll`, `update`
(overwrites the mutable columns by id), `hardDelete`, plus the binder-guide reads
(`listByBinder`, `ownedElsewhere`, `ownedCountsByDexNum`). The `Wishlist` repo has
the full CRUD surface: `save` (upsert parent + replace its source set), `find`,
`listAll`, `remove`, and `wishedDexNums` (the "Wished" status read). `app/` holds
`install_service`, `uuid` (`newUuidV4`, the shared id minter used by every
service), `BinderService` (binder CRUD verbs), `BinderGuideService`
(the `buildBinderEntries` the inferred zone refers to), `PokemonBrowseService`
(`listAll` → every catalog species paired with its owned-copy count, the unscoped
Pokédex browser's data), `CardCopyService` (the copy verbs —
`create`/`editDetails`/`assignToBinder`/`remove`[soft, with an optional
note-append]/`hardDelete`/`listAll`/`listByBinder` — with an injectable clock and
id generator like `BinderService`), `WishlistService` (the manage-sources verbs), and the
**card-catalog seam** — `CardCatalogApi` (Qt-free interface, parallel to
`PokemonExternalApi` but for *cards*), its concrete `PokemonTcgIoApi` (pokemontcg.io
URL/Lucene query building), the DTOs (`card_catalog_dto.h`: `CardSetInfo`,
`CardCandidate`), and `card_catalog_parse` (nlohmann/json parsers/mappers — see the
JSON note in the tech stack). A `CardSearchQuery` is scoped EITHER by species
(`dexNumber`, → `nationalPokedexNumbers:N`) OR by card name (`nameQuery`, →
`name:"…"`) — the latter is how a species-free card is found; on the GUI side
`CardSearchService::searchByName` and `CardFinderPanel`'s name-search mode drive it. `gui/views/` holds the
`MainWindow` shell (a macOS-style sidebar selecting sections in an outer
`QStackedWidget`: Binders, Pokémon, My Cards, Wishlist), the first-run setup dialog,
the binders section (`BindersPage`, a table with its own list ⇄ binder-guide stack),
the new-binder editor, the reusable `BinderPickerDialog`, the binder guide view, the
Pokémon browser (`PokemonListView`, which hosts an inner stack for the add-copy
page), and two card-copy pages built from the same two shared blocks — the reusable
`CardCopyForm` (the details pane: printed-identity/condition/ownership fields + binder
picker + comments, with `setReferenceEditable()` toggling read-only and a host-filled
action row) and the reusable `CardFinderPanel` (the set-scoped search + infinite-scroll
printings list + preview; reports picks via signals, knows nothing of forms/copies,
and exposes `setPreviewFooter()` for a host action under the picture). The
`AddCardCopyPage` assembles them editable (finder pick autofills the form; submit
creates a copy); the `EditCardCopyPage` assembles them read-only-but-comments (a
copy's first edit surface — edit comments with an explicit "Save comments" via
`CardCopyService::editDetails`, and change the image by re-searching the catalog
["Use this card's image", centered under the preview] or uploading a photo, staying
on the page after each save). `AddCardCopyPage` does double duty by its
`std::optional<PokemonDexNum> dexNumber` ctor arg: with a dex number it is a
species' add-copy page (finder species-scoped); with `nullopt` (opened from "My
Cards") it is the **species-free** add page for a non-Pokémon card, with the finder
in by-name search mode (`CardFinderPanel::NameSearchMode`). `OwnedCardsView` ("My
Cards" inventory: browse/search/assign-to-binder/remove, plus an **"Add a card…"**
button — always available, even when empty — that pushes the species-free
`AddCardCopyPage`, the only place a non-Pokémon card can be recorded) hosts the
add/edit pages on an inner stack and
reloads on return so an edited comment shows. The `CardImagePanel` is the "My Cards"
right-hand detail panel (title + image + the copy's comments beneath). The unscoped
wishlist section (`WishlistView`), and the
per-Pokémon wishlist editor (`WishlistSourcesEditor`) embedded below the artwork in
`PokemonDetailPanel`. `PokemonDetailPanel` is shared by the Pokémon browser and the
binder guide and has an **opt-in copy mode** (3-arg `showPokemon` + a `CardImageStore*`
ctor arg): when the binder guide hands it the species' owned copies filed in that
binder, it shows one copy's data (printed identity/condition/ownership/comments) plus a
counter and an "Edit card…" button (`editCopyRequested`), and swaps the picture to the
copy's card scan with a fallback to the Pokémon artwork. One copy is picked at random on
each row selection; returning from the edit page re-shows the just-edited copy
(`preferCopyId`) rather than re-rolling. Passing no copies (the Pokémon browser, which
passes no store) is the unchanged artwork-only path. The shared `scaled_pixmap.h` helper
(DPR-aware fit-scale) backs every image panel. Copy-label helpers (`speciesName`,
`cardText`, `speciesOrCardName`, `titleFor`) are header-only in `card_copy_labels.h`,
shared by My Cards and the binder guide's panel. Other enum→label display helpers are
header-only in `gui/views/`
(`region_labels.h`, `status_labels.h`, `condition_labels.h`, `ownership_labels.h`).
Every outbound external-API GET goes through one chokepoint: `gui/services/network_log.h`'s
`loggedGet(nam, request)` (used in place of a bare `nam_->get(request)`), which logs the URL
under the `pokedex.net` `QLoggingCategory` and bumps a per-host session counter — so there is a
single place that sees and tallies every call we make to a free public API (answering "are we
hammering some API"). Silence it with `QT_LOGGING_RULES="pokedex.net.info=false"`. Each service
keeps its own `QNetworkAccessManager` (ownership/abort semantics unchanged); `loggedGet` only wraps
the `get`. `gui/services/` holds `MediaService` (Pokémon artwork fetch+cache), `CardSearchService`
(card search transport — **no disk cache**: search results are display/memory-only),
and `CardImageStore` (the on-disk store for the one image a committed copy keeps,
`cards/<copyId>.png`; `save`/`fetchAndSave`/`load`, and an `imageChanged(copyId)`
signal so a view re-reads the file after a deferred download or an override lands).
`gui/models/` is still empty — no Qt item models yet; views adapt core →
widgets inline.

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
- **A `CardCopy`'s species is optional.** `pokemonDexNum` is
  `std::optional<PokemonDexNum>`: most cards depict a species, but a TCG
  collection also holds cards that depict none — Trainer/Energy cards, promos.
  A species-free copy (nullopt) is fully supported (create/edit/image-search/
  file-in-binder) but appears only in the flat "My Cards" inventory, never in a
  species-oriented projection (the Pokémon browser, a binder guide). Storage
  encodes nullopt as `0` in the `NOT NULL pokemon_dex_num` column (real dex
  numbers are ≥1) — the same sentinel convention as condition's `"" ↔ nullopt`,
  avoiding a nullable-column table rebuild. `CardReference` carries a `name` (the
  printed card name) so a species-free card has a real label, not just set/number.
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
- **Never open the user's real workspace from tests or verification runs.** The
  app resolves its data directory (SQLite DB + media) from
  `~/.config/pokedex-tcg` — or `$XDG_CONFIG_HOME`, or `$POKEDEX_TCG_CONFIG_DIR`
  (see `storage/workspace.cpp`) — which is a real collection someone may be
  using. So every test must isolate its storage: an in-memory `Database`
  (`Database{":memory:"}`), a `TempDir`, or a `POKEDEX_TCG_CONFIG_DIR` /
  `XDG_CONFIG_HOME` override into a temp dir (see `tests/storage/workspace_test.cpp`
  and `tests/app/install_service_test.cpp` for the `ScopedEnv` pattern) — the
  suite must never write to the default location. Likewise, when driving the
  built GUI to verify a change, launch it with
  `POKEDEX_TCG_CONFIG_DIR=<throwaway dir>` so you exercise a scratch workspace,
  not the user's — two instances writing the same DB also contend on SQLite's
  file lock.

**GUI navigation.** The app is a macOS-style shell: `MainWindow` has a left
sidebar (a `QListWidget` source list, Finder/Settings-style) selecting sections
in an outer `QStackedWidget`. Prefer navigating *within* the window — swap pages
in a `QStackedWidget` (as `BindersPage` does: binder table ⇄ binder guide, with
a Back button) — over opening a second top-level window. A separate window or
modal dialog is a deliberate, rare exception (the first-run setup, the
new-binder form, and the About box are three), not the default for showing more
detail. The one menu bar lives on `MainWindow` (attached via `layout->setMenuBar`,
since the shell is a plain `QWidget`, not a `QMainWindow`); its single "About"
`QAction` carries `QAction::AboutRole` so macOS relocates it into the application
menu. Because that native menu is easy to miss, the same dialog is also reachable
in-window from a muted "ⓘ About" footer button pinned at the bottom of the sidebar
(the sidebar list is wrapped in a `sidebarPanel` `QWidget` that carries the pane's
width bounds and stacks list-over-footer); the menu action and the button both fire
one shared `showAbout` lambda. The `AboutDialog` (`gui/views/about_dialog`) shows the app heading + "by
Mazuh", the build version, the description, and the repo/MIT-license links plus the
fan-project legal disclaimer (verbatim from the README). Its version string comes
from `gui/version.h`, generated **at build time** by the `pokedex_version_header`
target (`cmake/GenerateVersion.cmake`) as `pokedex::kAppVersion` = the last commit's
7-char short hash, with a `-dev` suffix on any non-`Release` build (dev.sh / a bare
configure) and none on the `Release` install (install.sh); it degrades to
`"unknown"` outside a git checkout. Build-time (not configure-time) generation is
deliberate so the hash tracks HEAD across commits without a reconfigure — dev.sh
only reconfigures when `build.ninja` is absent. Display
strings stay out of Qt-free core: a GUI-side helper maps enums to labels
(`region_labels.h`, `status_labels.h`), kept separate from the storage tokens.

**Long lists paginate by infinite scroll, not Prev/Next.** The default for a
long, scannable list is incremental loading (`PokemonListView`): render a chunk,
append the next as the user scrolls near the bottom (drive it off
`verticalScrollBar()`'s `valueChanged` for scroll-to-bottom plus `rangeChanged`
with a `maximum()==0` guard so a short first chunk still fills the viewport).
Append rows — never rebuild what's on screen. Reach for explicit paging controls
only when infinite scroll genuinely doesn't fit.

**Table columns sort on a header click, via `installHeaderSort`.** Every table
view (`BindersPage`, `OwnedCardsView`, `WishlistView`, `BinderView`) makes its
column headers sortable through the shared `installHeaderSort` helper
(`gui/views/sortable_table.h`) — do the same for any new table. Do **not** use
Qt's `setSortingEnabled()`: it reorders the `QTableWidget`'s rows in place, which
breaks these views because each maps a row index back to a parallel data vector
(`loaded_[row]`, `entries_[row]`) and rebuilds per-row search haystacks — the rows
would then point at the wrong records. Instead the helper reports the clicked
column + order; the view stores that as its own state (`int sortColumn_ = -1;
Qt::SortOrder sortOrder_`) and, in its existing `refresh()`/`reload()`, sorts its
**own data vector** with a typed comparator (numeric dex #, chronological
`Timestamp`, condition rank, `localeAwareCompare` for text — the `compareValues`
helper gives the -1/0/+1 shape) before repopulating, so rows stay aligned with
their backing data. `sortColumn_ < 0` means "unsorted — keep the natural load
order"; the state is re-applied on every refresh so sorting survives a reload. Use
`std::stable_sort` so equal keys keep their prior order. When rows are a fan-out of
the data (Wishlist: one row per source within a per-species entry), flatten to a
one-record-per-row vector first so every column — including the per-row one — is
sortable.

Two rules keep a header-click cheap and correct. **A sort is a pure in-memory
reorder — it must not re-hit storage.** Split each view's "load data" from its
"sort + rebuild rows": the data load (`reload()`/`refresh()`) queries and then
delegates to a `repopulate()` that sorts the cached vector and rebuilds the table;
the `installHeaderSort` callback calls only `repopulate()`. Cache whatever the
rebuild needs (`OwnedCardsView` holds `binderList_`, `WishlistView` its flattened
`rows_`) so reordering never re-queries — a header click on the binder guide must
not recompute every species' `CollectionStatus`. **Restore the selection by
identity in `repopulate()`, never by row index** (copy id, binder id, `(dex,
source)`): a sort moves the highlighted record to a new row, so a row-index
selection would leave row actions (Remove/Assign/Edit) silently targeting the
wrong record, and the binder guide's copy-mode panel would re-roll a random copy —
capture the shown copy id (`PokemonDetailPanel::shownCopyId()`) and re-show it.
**Precompute sort keys once per row**, not per comparison: decorate the rows into a
struct carrying the derived keys (a species/catalog lookup, a `QString`
allocation), sort that, then reorder the backing vector — the pattern
`WishlistView`'s `SourceRow` uses; a comparator that recomputes both operands' keys
does it `O(n log n)` times.

**An optional/nullable column must sink its unset rows to the bottom in *both*
directions.** The shared sort shell (`applyColumnSort`) flips the comparator's sign
for a descending sort (`ascending ? cmp < 0 : cmp > 0`), so a fixed "unset is
largest" sentinel (e.g. keying `nullopt` as `INT_MAX`) only sinks it on the
ascending click — descending then floats every blank row to the *top*. "No data"
isn't a low value; it belongs last regardless of direction. Key the field as
`std::optional` and compare via a direction-aware helper that pre-inverts the
set-vs-unset case for the shell's flip (see `compareOptionalRank` in
`owned_cards_view.cpp`, used for the Condition/Rarity/Foil columns).

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
