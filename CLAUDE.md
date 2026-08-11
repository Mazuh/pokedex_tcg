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
  parse the external card-catalog API responses (pokemontcg.io for metadata, tcgdex for
  prices) in `core/app/card_catalog_parse`. It never appears in a public core header (the
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
                          CardOwnership, CardBinderPocketGrid, CardBinderBlank);
                          inferred (CollectionStatus,
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
`card_copy.ref_set_name`; v3 added `card_copy.ref_name`, the printed card name;
v4 `card_copy.rarity`; v5 `card_copy.foil`; v6 added `card_set_cache` +
`cache_meta`, the TTL'd local cache of the external `/v2/sets` table — reference
data, not collection source-of-truth, see the `CardSetCache` note below; v7 added
`card_price` + `card_price_fetch`, the on-demand cache of a card's market prices —
also external reference data, keyed by the tcgdex card id and independent of
any `card_copy`, see the `CardPriceCache` note below; v8 added
`card_copy.external_card_id`, the link from an owned copy to its priced tcgdex card
[blank = unlinked] so the copy's prices can be looked up — resolved invisibly from the
copy's printed set+number on the first Fetch [`CardCopyService::linkCatalogCard`
persists it], no catalog search or user action involved; v9 rebuilt `card_set_cache`
with a `source` column [PK `(source, id)`] so ONE table + one `CardSetCache` class caches
BOTH providers' set lists — pokemontcg.io [`source='pokemontcg'`] for search narrowing and
tcgdex [`source='tcgdex'`] for price resolution — rather than a bespoke cache per vendor;
the migration PRESERVES the existing rows re-tagged `source='pokemontcg'` [keeping the outage
fallback] and renames the old fetch stamp; tcgdex populates on first price use; v10 added
`card_price_suppression` [`(external_card_id, provenance)` PK], a per-card, per-vendor "hide this
vendor's price" — kept in its OWN table, deliberately apart from `card_price`, so a Refresh
[which rewrites `card_price`] never disturbs a suppression and only Clear
[`CardPriceCache::clear`, which now also deletes suppressions] drops it; see the vendor-suppression
note below; v11 added `card_binder_region` [`(binder_id, region)` PK, `ON DELETE CASCADE`], the
join table making a binder's region **multivalued** — a binder can now be scoped to more than one
region [e.g. a "Kanto + Johto" album]. The v1 `card_binder.region` column becomes vestigial [left in
place, never read again — additive migrations don't rewrite v1's table]; the migration backfills each
existing single-region row into the join table, and `CardBinderRepository` reads/writes only the join
table thereafter [mirroring `wishlist_source`: `add`/`update` write the parent then the region rows in
one transaction, `listAll` attaches them in a second pass sorted to canonical enum order]; v12 added
`card_binder.capacity` / `pocket_rows` / `pocket_columns`, the album's optional **physical layout**
[`INTEGER NOT NULL DEFAULT 0`, 0 = unset — so every existing binder backfills to "not recorded" and
`DEFAULT 0` *is* the backfill]. Capacity is stored INDEPENDENTLY of the grid, never derived as
pages×pocketsPerPage [the page count isn't recorded], and is never enforced — a stuffed binder is
real, so the guide displays how full it is and blocks nothing; v13 added `card_binder_blank`
[`(binder_id, before_dex_num, before_copy_id)` PK, `ON DELETE CASCADE`], the deliberate **empty
pockets** that let a user control where a page breaks — see the blank-pockets note below),
so a fresh DB runs the whole chain and an existing one
only the tail — bump `kSchemaVersion` and add a step (never edit `kSchemaV1`) when
the schema changes.

**From v12 on, a migration step ALTERs a v1 table, so "migrate a fresh DB, then roll
`user_version` back and re-migrate" is no longer a safe way to exercise a step in a test** — the
replayed `ADD COLUMN` fails as a duplicate, and it can't be undone without `DROP COLUMN`, whose
availability varies with the system SQLite across the two CI legs. Stand the older shape up BY HAND
instead (`tests/storage/database_test.cpp`'s v8/v9/v11 tests are the pattern). `storage/` holds
workspace resolution, the SQLite `Database` wrapper + schema/migrations, the
`codecs` (region/ownership/condition enums ↔ tokens, timestamps ↔ ISO-8601),
and repositories for `CardBinder`, `CardCopy`, and `Wishlist`. The `CardCopy`
repo now carries the full CRUD surface — `add`, `find`, `listAll`, `update`
(overwrites the mutable columns by id), `hardDelete`, plus the binder-guide reads
(`listByBinder`, `ownedElsewhere`, `ownedCountsByDexNum`). The `Wishlist` repo has
the full CRUD surface: `save` (upsert parent + replace its source set), `find`,
`listAll`, `remove`, and `wishedDexNums` (the "Wished" status read). `app/` holds
`install_service`, `uuid` (`newUuidV4`, the shared id minter used by every
service), `BinderService` (binder CRUD verbs — `create`/`update` both take a
`std::vector<Region>` for the multivalued region set plus the optional `capacity` +
`CardBinderPocketGrid`; `update` replaced the old name-only `rename`; and
`insertBlanks`/`removeBlanks`, the blank-pocket verbs. **Every mutating verb RETURNS the
persisted `CardBinder`**, read back through the new `CardBinderRepository::find` — the GUI
keeps a by-value binder it never re-reads, so returning the whole entity lets a host replace
its copy wholesale [`binder_ = service.insertBlanks(...)`] instead of patching the fields it
happens to know about, which is what stops a newly added field from being silently dropped on
that path), `BinderGuideService`
(the `buildBinderEntries` the inferred zone refers to — one row per card FILED in the
binder [a species' duplicates adjacent in filed order; species-free cards last, since they
carry no dex number] plus one placeholder row per listed species holding nothing, plus one
row per **blank pocket** interleaved before the row its anchor names; a row is a
slot, not a species), `PokemonBrowseService`
(`listAll` → every catalog species paired with its owned-copy count, the unscoped
Pokédex browser's data), `CardCopyService` (the copy verbs —
`create`/`editDetails`/`assignToBinder`/`remove`[soft, with an optional
note-append]/`hardDelete`/`listAll`/`listByBinder` — with an injectable clock and
id generator like `BinderService`), `WishlistService` (the manage-sources verbs), and the
**card-catalog seam** — `CardCatalogApi` (Qt-free interface, parallel to
`PokemonExternalApi` but for *cards*), its concrete `PokemonTcgIoApi` (pokemontcg.io
URL/Lucene query building), the DTOs (`card_catalog_dto.h`: `CardSetInfo`,
`CardCandidate`), `card_catalog_parse` (nlohmann/json parsers/mappers — see the
JSON note in the tech stack), and `CardSetCache` (the cross-launch persistence of a set
table — a `vector<CardSetInfo>` — in the `card_set_cache`/`cache_meta` tables). A
`CardSetCache` is **scoped to one `source` at construction** (`"pokemontcg"` / `"tcgdex"`):
every read/write is filtered to that source and rows for both providers coexist in the one
table (keyed by `(source, id)`), with a per-source fetch stamp (`sets_fetched_at:<source>`).
So the catalog's set list (search narrowing) and the pricing provider's (price resolution)
share one table + one class + one TTL/disk-cache mechanism instead of a bespoke cache each —
the two just pick different sources. It lives in `app/` rather than `storage/` precisely
because its row type `CardSetInfo` is an app projection, so a storage-layer repo would invert
the layering: `app/` may use `storage/`, not vice versa. The **card-price seam** uses a
**separate pricing provider from the metadata catalog**: pokemontcg.io remains the
metadata source (search/autofill/images), but **tcgdex** (`api.tcgdex.net`, a free
aggregator — a *pricing provider*, not an authoritative source of truth) is the **sole
automated pricing feed**. It is used because — unlike pokemontcg.io — it covers
brand-new sets and is addressable by set+collector-number, so a card the metadata
catalog hasn't ingested (a fresh promo like MEP) can still be priced. The seam:
`card_catalog_parse::parseTcgdexCardPrices` (pure parser of a `/v2/en/cards/{id}` payload
— the `variants_detailed[].pricing.{tcgplayer,cardmarket}` blocks — one `CardPrice` row
per vendor×variant×metric, dropping ≤0-cent noise; tcgdex's metric NAMES are **normalized
to the same canonical vocabulary** the pokemontcg parser emitted [`trend`→`trendPrice`,
`marketPrice`→`market`, …] so the cache and every display helper stay source-agnostic;
only whitelisted price keys are read, since the blocks also carry non-price numbers
[`idProduct`/`productId`] a blind scan would misread as money; ISO-8601 `updated` date →
midnight-UTC `observedAt`, clock-free), `parseTcgdexSets` (the flat `/v2/en/sets` array →
`vector<CardSetInfo>`) and `resolveTcgdexCardId(ref, sets)` (a copy's printed set+number →
its tcgdex id, no search — only EXACT set matches are trusted [set name first, then the
printed code AS a tcgdex set id for promos]; an unidentifiable set returns nullopt rather
than a fuzzy guess, since a wrong link would show another card's prices),
`CardPriceCache` (thin SQL over `card_price`/`card_price_fetch`/`card_price_suppression`,
mirroring `CardSetCache`), and `CardPriceService` (the verbs, with an injectable
clock/uuid like `CardCopyService`: `recordTcgdexPrices` [parse a fetched payload, replace
the card's API-sourced rows, preserve `manual` rows, stamp the fetch — sharing its persist
path with the retired `recordApiPrices`], `addManualPrice`/`removeManualPrice` [the human
override — manual entry is the escape hatch for anything tcgdex can't price],
`suppressVendor`/`unsuppressVendor`/`suppressedVendors`/`suppressedVendorsForMany` [the
vendor-suppression verbs — see the note below], `pricesFor`,
`fetchedAt`, and `needsRefresh(key, ttl)` — the anti-hammer TTL gate). Prices are keyed by
`external_card_id` — a **source-neutral** card identity (named for what it is, an external
catalog's card id, not for any one provider) that is deliberately **independent of
`card_copy`** (deleting a copy never removes pricing; a card carries many prices at once
because there is no single true price). Today that id is always a tcgdex card id
(`"mep-013"` = setId+"-"+localId), derivable directly from the copy's set+number; the
source-neutral naming keeps the schema open to another provider without a rename. Money is
stored as integer
cents (`Statement::bindInt64`/`columnInt64`), and multi-statement writes go through
`Database::transaction(body)` (the shared BEGIN/ROLLBACK/COMMIT guard); the TTL
freshness rule (incl. the backward-clock guard) is the shared `cacheIsFresh` helper
(`core/app/cache_ttl.h`), used by both this cache and the GUI set cache. The transport
GET stays GUI-side: `gui/services/CardPriceLookupService` (mirrors `CardSearchService`,
sharing the `gui/services/http_status.h` retry-classification helpers) fetches one card's
prices from tcgdex's `/v2/en/cards/{id}` and persists them through `recordTcgdexPrices`; it
also **lazily loads the tcgdex set table** so `resolveTcgdexId(ref)` can map a copy's
set+number to a tcgdex id with no search: `ensureTcgdexSets` reads it from the tcgdex-scoped
`CardSetCache` (disk, no network) when fresh within a 24h TTL, else fetches `/v2/en/sets`,
persists it, and falls back to a stale cached copy if the fetch fails — the same disk-cache
treatment the catalog set table gets (mirrors `CardSearchService`), so most launches skip the
fetch entirely. It builds tcgdex URLs directly (no `CardCatalogApi` — pokemontcg.io's
`resolveCardById` was removed with the pokemontcg price path). It is strictly **on-demand**
— `cached`/`fetchedAt` never touch the network; only `fetch()` and the one-time set-table
load (both behind an explicit Fetch/Refresh) do — so merely viewing a card never hits the
API. It also drives the **bulk refresh**: `refreshMany(ids)` re-fetches many linked cards with
a bounded-concurrency queue (`pumpBulk`/`advanceBulk`, cap `kBulkMaxConcurrent`), emitting
`bulkProgress`/`bulkFinished`. Because there is ONE shared service the cap and the "one bulk at
a time" guard are **global** across views, and the queue advances only from
`finishSucceeded`/`finishFailed` (real fetch completions) so a `suppressVendor`/`clearPrices`
that also emits `pricesReady` can't miscount it. It backs the **"Refresh prices"** button on the
binder guide (`BinderView`) and My Cards (`OwnedCardsView`) — both over their **non-Removed**
copies, so every row that can display a price can also refresh it (a Removed copy is frozen
history: blank cell, never fetched). Note the binder's header **value** total is narrower still,
Owned-only — a card on its way to you isn't yet worth anything to the binder — so the two
predicates deliberately differ. It is a manual "update now" that re-fetches every linked card
**regardless of TTL** (intentional, see the price-cache-tradeoffs memory); each view skips its per-card
`pricesReady` rebuild while `bulkRunning()` and does ONE rebuild on `bulkFinished`. **Pricing is
split into a read-only-ish summary and a management page — the same move the wishlist made off
the crowded inspector.** Every owned-copy surface (the Edit page, the My Cards detail
`OwnedCardsView`, and the binder-guide / Pokémon-browser copy detail `PokemonDetailPanel` copy
mode) shows `gui/views/CardPricesSummary` (`showCopy(copy)` / `clear()`): the per-vendor headline
figures (suppressed vendors filtered out), each vendor **name itself the link** to a marketplace
search, the ⓘ metrics/freshness popover, an inline **Fetch/Refresh**, and a **"Manage prices"**
button. It never networks on mere selection (renders the cache, re-renders on `pricesReady`); its
one active affordance is the inline Fetch/Refresh, which drives the **shared
`gui/views/CardPriceFetchController`** — the fetch/resolve/link state machine BOTH this summary
and the management panel use, so the inline button gets the same invisible resolve-and-link
(first fetch links an unlinked copy; a legacy pre-tcgdex id is re-resolved) and never dead-ends.
The controller has two entry points: `fetch()` (the MANUAL Fetch/Refresh — always hits the wire,
ignoring the TTL, an explicit "get the latest") and `autoFetch()` (the AUTOMATIC on-add fetch —
same resolve/link, but skips the network when the card's cached prices are still fresh
[`CardPriceLookupService::pricesFresh`, a 24h window], so a booster's repeated same-card adds
don't each re-hit the free API). **A new copy auto-fetches its prices on add**: `AddCardCopyPage`
(which now takes the `CardPriceLookupService`) spawns a fire-and-forget `CardPriceFetchController`
parented to the app-wide price service (NOT the page, which the host disposes on Back), calls
`autoFetch()`, and lets it self-delete on completion — so the user no longer presses Fetch after
every add. Because the page is gone by the time a COLD tcgdex set table finishes resolving, the
controller's `cardLinked` is relayed up as the app-wide `CardPriceLookupService::copyAutoLinked`
signal; every owned-copy host (`OwnedCardsView`, `PokemonListView`, `BinderView`) connects it to
write the resolved id into its in-memory copy vector/buckets (the same `applyLinkedCardTo…` its
panel `copyLinked` uses), so the auto-fetch's follow-up `pricesReady` isn't dropped by the host's
`anyCopyLinkedTo` guard and the Prices column fills in.
A fetch that links the copy is relayed up via `copyLinked` (hence the summary needs
`CardCopyService` for the controller). Its Manage button emits `managePricesRequested(copyId)`,
which the host turns into a push of `gui/views/PricesEditPage` (Back bar + card heading) — the
single home for the heavier verbs (Clear, hide/restore), which **hosts the interactive
`gui/views/CardPricesPanel`**. That
panel (unchanged) is driven by `showCopy(copy)` / `clear()` (it carries the copy's link
context — id, `CardReference` — not just an `external_card_id`) and renders the states
[nothing-selected / unresolvable (no set/number) / ready-to-fetch (resolvable OR already
linked) / has-prices (a headline of one figure per vendor, one vendor **per line**, each with
a ✕ hide affordance; the "as of"/fetched dates live on the ⓘ tooltip) / fetched-empty], plus
Fetch/Refresh, Clear, and per-vendor hide/restore. There is **no "show all prices" table** —
the full per-metric spread is deliberately left to the marketplace links, so the panel stays
a compact headline + Fetch/Refresh + hide + ⓘ. The push is wired through two host helpers:
`pushPricesPage` (page push + teardown + Back→`onReturn`, forwarding the panel's `cardLinked`
to `onLinked`) and `openPricesFromBuckets` (its bucketed-by-dex guard, mirroring
`edit_copy_page_host.h`); the **Pokémon browser** (`PokemonListView`, whose row is a species) uses
the buckets variant over `owned_`, while the two surfaces whose row is a CARD — My Cards (a flat
`loaded_` vector) and the binder guide (`BinderView`, over `filedCopies_`) — call
`pushPricesPage` directly. Do NOT reintroduce a bucketed lookup in the guide: the buckets are
keyed by species AND Owned-only, so it would silently drop exactly the rows the per-copy row
model exists to show — species-free cards (no bucket) and Incoming ones. The **Edit page pushes it onto its own inner `QStackedWidget`**
(page 0 = the edit content) so managing prices stays in-window without a second Back fighting
the page's own. On Back the host re-shows the copy (and `BinderView`/`OwnedCardsView` reload,
since a Clear/hide changes their Prices column / value total). The shared headline renderer
lives in `gui/views/price_headline.h` (`headlineHtml(…, withHideLinks)` — the summary passes
false so the ✕/restore are page-only; `marketSearchTerm`, `marketplaceSearchUrl`,
`priceInfo…`), so the two surfaces can never format a figure or a link differently. Pricing
resolves from set+number, not a search, so neither surface needs `CardSearchService`.
**Linking is invisible — never a UI verb.** A copy that isn't yet linked but records a set +
collector number still shows a "Fetch prices" button; the first Fetch resolves the tcgdex
card id directly from that printed set+number (`onFetchClicked` → `ensureTcgdexSets` →
`resolveTcgdexId` → `linkCatalogCard`), persists the link, then fetches — all as one action,
emitting `cardLinked` so the host updates its cached copy. A copy still linked to a pre-tcgdex
id is transparently **re-resolved** on its next Fetch (the resolve runs whenever the copy is
resolvable, updating the link only when the id changed). An unidentifiable set is reported
("couldn't identify this card for pricing — check its set and number in Edit"), never guessed;
too little data (no set/number) shows a hint to complete it in Edit. (There is no longer a
manual "Link prices to this card" button on the Edit page — that was pokemontcg-specific and
is superseded by the invisible set+number resolution; disambiguating a genuinely ambiguous
set name is a possible future enhancement.) The panel also carries an
"ⓘ" popover (same idiom as the card-attribute pickers) explaining the metrics **and the
price freshness** (the vendor "as of" date + the day we fetched, set per-render). The
per-vendor **listing links** are merged **into the headline** — the vendor name is itself
the link ("TCGplayer ↗ $350.00"), pointing at a **marketplace name-search** for the card
(tcgdex carries no stable per-listing URL we persist, so the vendor name searches that
marketplace rather than deep-linking a product page). The headline's per-vendor pick
(`vendorBest`) never selects the TCGplayer `high` outlier, so it can't surface. **Finish-aware
pricing:** `vendorBest`/`priceAmountsInline`/`accumulateBestPrices` take a `preferredFinish`
(`finishForFoil(copy.foil)` → tcgdex's `normal`/`holo`/`reverse`; the finer treatments all map
to `holo`, an unset foil to `""`), so within a metric `bestPrice` prefers the row whose variant
matches the copy's finish (a non-holo copy shows the **normal** figure, not the pricier holo one)
and only falls back to the highest when no variant matches — the parser tags each row's finish
(`card_catalog_parse::canonicalFinish`) precisely so `normal`/`holo`/`reverse` stay distinct
here. **Vendor suppression:** each headline figure carries a muted "✕" (an in-app
`action:hide:<vendor>` link, routed by `CardPricesPanel::onHeadlineLinkActivated` — http vendor
links open a browser, `action:` links call `suppressVendor`/`unsuppressVendor`) that hides a
vendor whose tcgdex mapping is wrong for the copy; a hidden vendor that still carries a price
shows a "<vendor> hidden — restore" line instead. A suppression **persists across Refresh** and
is dropped only by **Clear** (the one ground-zero reset — prices, fetch stamp, AND suppressions).
`filterSuppressed` (in `price_labels.h`) drops suppressed rows before every display path — the
panel headline AND both card tables' Prices column / value total / price-sort — so a hidden vendor
never surfaces anywhere; the tables load a batched `suppressedVendorsMany` map beside the price
map (`loadSuppressedVendorsFor` in `owned_copy_buckets.h`) and read through `visiblePricesForCopy`.
NOTE: `vendorBest` returns pointers INTO the prices vector it is handed, so a caller must bind
`filterSuppressed(...)`/`visiblePricesForCopy(...)` to a **named local** before calling
`vendorBest` and then reading the result across statements — passing the temporary inline dangles
those pointers (this bit the panel headline once, rendering "0.00"). Prices ride along for free while **browsing**:
`parseCardSearchResponse` extracts the same tcgplayer/cardmarket blocks the search payload
already carries into `CardCandidate.prices` (display-only, never persisted), and
`CardFinderPanel` shows a subtle headline under the preview — no extra HTTP for a card the
user may not own. Money→string display (currency symbols, the headline pick) is
GUI-side in `gui/views/price_labels.h`, shared by the finder hint and the panel so they
never diverge. `priceAmountsInline` (labels-free "$… · €…", the currency symbol as the
only context) backs the **Prices column** both card tables carry (My Cards, the binder
guide): each row shows ITS OWN copy's cached figures (a binder-guide row is one card, and a
placeholder row has none), read from a single batched
`cachedMany` snapshot per reload (`loadCachedPrices` — no network, so a header-sort never
re-queries), blank for an unlinked/unfetched/Removed copy, and sortable by summed
per-vendor value (USD+EUR added without an FX rate — the same intentional rough-magnitude
tradeoff as the price table's amount sort; unpriced rows sink to the bottom). Edition modeling and manual-price entry remain deferred.
A `CardSearchQuery` is scoped EITHER by species
(`dexNumber`, → `nationalPokedexNumbers:N`) OR by card name (`nameQuery`, →
`name:"…"`) — the latter is how a species-free card is found; on the GUI side
`CardSearchService::searchByName` and `CardFinderPanel`'s name-search mode drive it. `gui/views/` holds the
`MainWindow` shell (a macOS-style sidebar selecting sections in an outer
`QStackedWidget`: Binders, Pokémon, My Cards, Wishlist, Settings), the first-run setup dialog,
the binders section (`BindersPage`, a table with its own list ⇄ binder-guide/edit
stack), the `BinderEditPage` (the create/edit-binder screen — see "GUI navigation"
below), the reusable `BinderPickerDialog`, the binder guide view (`BinderView` — **a row is a
card, not a species**: it renders `BinderGuideService`'s per-copy rows, so a Pokémon's several
copies sit side by side and a Trainer/Energy card gets a row of its own after the species rows
[blank "#"], while a species holding nothing keeps its placeholder row. Row identity is
therefore the COPY ID with the dex as fallback [`rowOf`], not the dex — restoring a selection
by species alone would silently retarget Edit/Prices at a different physical card. Because
each row names one exact copy, the panel is driven with `showSingleCopy`, and the panel's
sticky `setAddMode`/`setWishlistVisible` are set PER ROW [a species-free row switches Add to the
"add a card" flow and hides the wishlist] and reset by `clearPanel()`. A Removed copy's row is
grayed, has a blank Prices cell, and is inert on double-click. Its top bar carries a **"Add a
card"** button — the species-free add page locked to this binder, always available since a
Trainer card has no row to start from; the species add flow is unchanged. Header stats are four
tooltipped labels: `Listed` [distinct species — NOT `entries_.size()` any more] · `Captured`
[species with ≥1 Owned copy filed here] · `Cards` [Owned copies filed here; duplicates and
non-Pokémon cards each count — rendered as "Cards 42 of 360 (12%)" when the binder records a
capacity, and deliberately UNCLAMPED, since an over-full album is exactly what the figure
exists to reveal] · market value. Its first two columns, `Page` and `Pocket`, say where a row
physically sits — see the binder-layout note below), the
Pokémon browser (`PokemonListView`, which hosts an inner stack for the add-copy
page), and two card-copy pages built from the same two shared blocks — the reusable
`CardCopyForm` (the details pane: printed-identity/condition/ownership fields + binder
picker + comments, with `setReferenceEditable()` toggling read-only and a host-filled
action row; the binder picker pairs the combo with an optional **"Remove from binder"**
button beside it — hidden until `setBinderRemovable(true)` [only the edit page opts in], it
just selects the combo's "— None —" entry and emits `binderChanged`, reusing the host's
existing save-binder path rather than adding a second unassign verb, and is enabled only
while a binder is actually selected) and the reusable `CardFinderPanel` (the set-scoped search + infinite-scroll
printings list + preview; reports picks via signals, knows nothing of forms/copies,
and exposes `setPreviewFooter()` for a host action under the picture). The
`AddCardCopyPage` assembles them editable (finder pick autofills the form; submit
creates a copy). It carries a **"Reuse last info from “<card>”"** button (session-static
`LastAdded`, labelled with the last add's card/species name) for the same-booster flow:
it prefills only what the card search can't supply itself — the comment (into an empty box
only), language, and condition — and writes the last **set** onto the form while, in
species mode, also **driving the finder search** from it (`CardFinderPanel::searchFor`) so
the search does its natural job (list the set's printings, autofill the picked card's
identity/rarity/image). Writing the set to the form as a baseline is deliberate: it's never
lost if the search flakes/finds nothing (and pricing can still resolve from it), and
resetting the per-card identity (name/collector) to just the set means a previously picked
card can't be saved by mistake. The `EditCardCopyPage` assembles them read-only-but-comments (a
copy's first edit surface — edit comments with an explicit "Save comments" via
`CardCopyService::editDetails`, and change the image by re-searching the catalog
["Use this card's image", centered under the preview] or uploading a photo, staying
on the page after each save). `AddCardCopyPage` does double duty by its
`std::optional<PokemonDexNum> dexNumber` ctor arg: with a dex number it is a
species' add-copy page (finder species-scoped); with `nullopt` (opened from "My
Cards") it is the **species-free** add page for a non-Pokémon card, with the finder
in by-name search mode (`CardFinderPanel::NameSearchMode`). `OwnedCardsView` ("My
Cards" inventory: browse/search/assign-to-binder/remove/delete) hosts the add/edit
pages on an inner stack and reloads on return so an edited comment shows. Its
right-hand detail is the **shared `PokemonDetailPanel`** (see below), configured
species-free-friendly (`setAddMode(FreeCard)` → an always-enabled "Add" that records a
non-Pokémon card — the only place one can be recorded; `setWishlistVisible(false)`); the
inspector also hosts the "Edit" action, so the left toolbar keeps only
Assign/Remove/Delete. The unscoped
wishlist section (`WishlistView`), and the
per-Pokémon wishlist editor (`WishlistSourcesEditor`) — no longer embedded in the
detail panel (it crowded the card info) but hosted on its own in-window page,
`WishlistEditPage` (a Back top bar + species heading over the reusable editor), pushed
onto the host's stack exactly like the add/edit copy pages. `PokemonDetailPanel` now
shows a compact "Wishlist (N)" / "Wishlist (none)" button (reading the source count via
`WishlistService`) that emits `editWishlistRequested(dex, name)`; both copy-mode hosts
(`PokemonListView`, `BinderView`) turn that into a `WishlistEditPage` push, and on Back
re-show the species so the counter refreshes (the binder guide also `refresh()`es, since
a wishlist change can flip a species' `CollectionStatus` between Missing and Wished).
The **Settings section** (`SettingsView`) is the app's configuration screen — today three
settings: the collection **workspace folder**, the **default card language**, and the **AI
assistant API key** (a masked field; the provider secret, stored under the vendor-neutral
config key `assistant_api_key` and read fresh per call so it applies live), all loaded
from and saved to the `config` file the app already uses (`storage/workspace.h`). That file
is now a tiny **`key=value` store** (`readConfigValue`/`writeConfigValue` over an internal
load→map→write, one setting per line, `std::map`-sorted; every write *merges* so one setting
never clobbers another) — with **back-compat** for the old single-bare-path format (a first
line with no `=` is read as the workspace path, and the first write upgrades the file);
`readConfiguredWorkspacePath`/`writeConfiguredWorkspacePath` are now thin wrappers over the
reserved `workspace` key. Unlike every other page (which writes straight through), Settings is
a **manually-applied form**: edits are staged in the fields and committed only by the accented
"Save changes" primary button, which validates+opens the workspace via `openWorkspace`
(creating/migrating it, the relaunch path) *before* recording both settings. A **workspace**
switch takes effect on the **next launch** (a muted note says so while the workspace field is
dirty — the note is workspace-only); the **default language** applies **live** — `AddCardCopyPage`
reads it (`readConfigValue(kDefaultLanguageConfigKey)`) fresh per open and pre-selects it on the
form for a new copy (reuse-last / a manual pick still override), so a change needs no restart.
The language code list is the shared `gui/views/language_codes.h` (also the config-key constant),
used by both the card form's Language picker and this screen so they can't drift. Because leaving
a staged form would silently drop edits, `MainWindow` **guards every section switch** (and the
window close) through `SettingsView::confirmLeave` (Save / Discard / Cancel); on Cancel it snaps
the sidebar selection back to Settings via a **queued** `setCurrentRow` (an inline revert gets
overwritten by the list's own in-progress key/click handling, leaving the highlight on the new
row while the page stays on Settings). `showEvent` reloads from config only when `!isDirty()`, so
a spontaneous re-show (Cmd+H / minimize) never wipes staged edits.
`PokemonDetailPanel` is the **single inspector** shared by all three card surfaces —
the Pokémon browser, the binder guide, and My Cards (`CardImagePanel` was deleted). Top
to bottom it renders: the card's name (falling back to the species name), a printed
identity line (the set abbreviation — or the full set name when there's no abbreviation —
plus the collector number, via `collectorLine`), the image (the copy's card scan, falling
back to the Pokémon artwork when there's a species), a condition + foil line, a rarity +
"N copies" line (N is the count of that species' live copies on this surface — a total,
soft-Removed copies excluded), the copy's comments, the read-only `CardPricesSummary` (figures
+ links + ⓘ + a "Manage prices" button; see the pricing note above), an **Add + Edit** button
row (side by side), and an optional "Wishlist (N)" button. Copy mode is opt-in (needs a
`CardImageStore*`): the **Pokémon browser** (whose row is a species) enters it via
`showPokemon(dex, name, copies, prefer)` (one copy shown — `preferCopyId`, else a random pick),
while the surfaces whose row is a CARD — My Cards and the binder guide — enter it via
`showSingleCopy(copy, sameSpeciesTotal)` (the exact selected copy; dex is optional, for
species-free cards). `setAddMode` picks the Add flow (SpeciesCopy → `addCopyRequested`;
FreeCard → `addCardRequested`); `setWishlistVisible(false)` hides the wishlist on My Cards.
Both are **sticky** state, not per-show arguments, so a host mixing species and species-free
rows (the guide) must set them on every selection and reset them when it clears the panel;
Edit is hidden for a soft-Removed copy. Passing no copies (the Pokémon browser on an
unowned species) is the artwork-only path. The shared `scaled_pixmap.h` helper
(DPR-aware fit-scale) backs every image panel. Copy-label helpers (`speciesName`,
`cardText`, `setLabel` [the "Set" column's "Base Set (BS)"], `collectorLine` [the
inspector's set+number line], `speciesOrCardName`, `titleFor`) are header-only in
`card_copy_labels.h`, shared by My Cards and the binder guide. Other enum→label display helpers are
header-only in `gui/views/`
(`region_labels.h`, `status_labels.h`, `condition_labels.h`, `ownership_labels.h`).
Every outbound external-API GET goes through one chokepoint: `gui/services/network_log.h`'s
`loggedGet(nam, request)` (used in place of a bare `nam_->get(request)`), which logs the URL
under the `pokedex.net` `QLoggingCategory` and bumps a per-host session counter — so there is a
single place that sees and tallies every call we make to a free public API (answering "are we
hammering some API"). Silence it with `QT_LOGGING_RULES="pokedex.net.info=false"`. Each service
keeps its own `QNetworkAccessManager` (ownership/abort semantics unchanged); `loggedGet` only wraps
the `get`. Its POST counterpart `loggedPost(nam, request, body)` (same chokepoint, for the AI
assistant) shares the per-host tally and logs **only the URL** — never headers/body — so a secret
carried in a request header never reaches the log.

The **AI-assistant module** is a vendor-neutral LLM seam, mirroring the card-catalog seam. It is
abstractly named so the provider (Gemini today) can be swapped by changing **one line in
`main.cpp`** with no caller, transport, config key, or UI change: `core/app/ai_assistant.h` is the
Qt-free `AiAssistant` interface + neutral DTOs (`AiPrompt`, `AiRequest`, `AiResult`) — it *builds*
the HTTP request and *parses* the response (BOTH behind the interface, since both shapes are
provider-specific), no I/O, headlessly testable; `core/app/gemini_assistant.*` is the sole
Gemini-aware class (generateContent URL, JSON body, response layout; the API key rides the
`x-goog-api-key` **header**, never the URL, so it can't leak to the log). The GUI transport
`gui/services/AssistantService` (a QObject) reads the key from config at call time (so a Settings
change applies live; missing key fails fast with a Settings hint), POSTs via `loggedPost`, and
emits `answerReady`/`failed`. `AssistantService::ask` has two overloads: `ask(QString)` (the text
demo) and `ask(const AiPrompt&)` (the richer entry — a system instruction, an inline image, the
JSON-response hint — used by the card scanner); both share one send path + the stale-reply guard.
The demo `gui/views/AssistantPromptDialog` (a modal prompt/answer box, opened from the Tools menu)
exercises the text path.
**Webcam card-scanning (BUILT).** The sidebar footer's **"✦ Scan card"** button (and Tools ▸
*Scan a card…*) opens `gui/views/ScanCardView` — an **in-window screen** (NOT a modal; it's a page
in the shell's `sections_` stack with no sidebar row, index after Settings, opened via `showScan`
and returned from via a **Back** top bar → `backRequested`, which pops to the section it was opened
from) with a live `QVideoWidget` preview (QCamera / QMediaCaptureSession / QImageCapture) and a
*Scan* button. Pressing *Scan* **freezes the frame** — the camera stops and the captured still
replaces the live preview (a `QStackedWidget` page swap), so the user sees exactly what was sent
while the assistant thinks; a **Retake** button (frozen-only; also an in-flight abort — it
`cancelPending`s) resumes the live camera. The camera runs **only while the page is shown**:
`startScan()` resets to a fresh scan and `showEvent` starts the camera (reusing one `QCamera`
across opens — don't recreate per `showEvent`), `hideEvent` stops it + `cancelPending`s (so leaving
the section OR minimizing turns the device light off; the reading is kept so restore resumes). The
captured frame is downscaled (≤1024px) + JPEG-base64-encoded GUI-side, then sent as a **vision
`AiPrompt`**: `AiPrompt` grew an `images` field (`std::vector<AiImagePart>` of `{mimeType,
base64Data}` — base64 done GUI-side so core needs no base64 impl) and a `wantsJsonResponse` flag (→
Gemini's `generationConfig.responseMimeType`), and `GeminiAssistant::buildRequest` folds images
into `inline_data` parts. **The LLM only READS the print, it never "finds" the card** — the
card-scan intelligence is the Qt-free `core/app/card_scan.{h,cpp}`: a **system instruction** that
asks for a JSON object, `buildCardScanPrompt(base64)`, and the tolerant `parseScannedCard(reply)` →
`ScannedCard { identified, cardName, setName, setCode, collectorNumber, query, note }` (strips
```-fences/prose, never throws, synthesizes `query` from set+number when the model omits it, and
degrades an unreadable card to `identified=false` + a `note`). The `query` is a plain search
string (a distinctive slice of the English set name — or the set code — plus the collector number,
e.g. `collection 2021 1/25`) tuned for the existing **flexible My Cards substring search** (which
matches a **contiguous** slice of a column-ordered haystack, so the query is NOT regenerated from
the full set name). The reader is instructed to return the card's **official ENGLISH name**,
translating from whatever language is printed ("Pikachu do Ash" → "Ash's Pikachu"). The reading is
shown **split in half**: LEFT = the **editable fields** (Card / Set / Set code / Number / Search) so
the user can fix a misread letter/digit (`cardResolved`/`addRequested` carry the fields **as
edited**, `currentReading()`); RIGHT = **"first impressions"** — a muted **owned-name match count**
("N of your cards may match this name", hedged since it's approximate) plus the **detected Pokémon**
(species name, dex #, region). Detection is the Qt-free, unit-tested `detectScannedSpecies` (in
`core/app/card_scan`): a **whole-word token match** of the card name against the National Pokédex
catalog — names are split on whitespace and stripped of intra-word punctuation/symbols (so
"Farfetch'd"/"Farfetchd"/"Nidoran♀" match but "Parasol Lady" does NOT falsely match "Paras"), the
longest match wins ("Mewtwo"≠Mew), and the catalog is tokenized once into a static index (no
per-keystroke rescan). MainWindow snapshots the live (non-Removed) collection's display names
(deduped by printing) and passes the owned-name matcher via `ScanCardView::setOwnedNameMatcher`, so
the view stays storage-free; the count is intentionally **name-based** (`owned.contains(scanned)`,
per the feature request) and thus complementary to the set+number search. The panel also shows a
**set-recognition** line ("Set recognized: Base Set (BS)" / "not recognized") — but ONLY when the
catalog's set table is **already loaded in memory**: `ScanCardView::setSetMatcher` takes a `SetMatcher`
that MainWindow wires to query `CardSearchService::sets()` **live** (set once — the service outlives
the view — and it **never triggers a fetch**; an unloaded table stays silent rather than reading as
"unrecognized"), resolving the read code/name to a set **precisely** via a file-local `matchReadSet`
(exact printed code → exact set name → a *uniquely* matching name-substring; ambiguous/absent → "not
recognized", never an arbitrary pick — deliberately unlike the search-narrowing `resolveSetFilterToIds`)
and labelling it with the shared `setLabel` formatter. It recomputes on any set-field or card-name
edit. The panel has a
*Search my cards* action (emits `cardResolved` → `MainWindow` fills the My Cards live search with
`query` and switches there; the scan is **stashed on `OwnedCardsView`** and the NEXT *Add a card*
consumes it via `AddCardCopyPage::prefillFrom`) plus **two direct-add buttons** that **skip the
search and go straight to creation** — both emit `addRequested(reading, dexNumber, copyFieldsToForm)`
(the `bool` picks the prefill). MainWindow routes via a shared `goToSection(row)` (which handles the
"sidebar already on that row → switch the page directly" case) to the species add page (dex>0) or the
species-free one (dex==0), and then, per `copyFieldsToForm`:
- **"Create by set" / "Create by name"** (`copyFieldsToForm=false`) — seed the catalog **finder
  search** and let the picked printing autofill deterministically (the reliable path when the search
  works). Species → `PokemonListView::openAddCopyBySet` → `AddCardCopyPage::prefillSetSearch` (the
  set **code**/abbreviation, falling back to the full set name when the code is <3 chars since the
  finder only auto-searches at 3+); non-Pokémon → `OwnedCardsView::startAddScannedCard(…, false)` →
  `AddCardCopyPage::prefillFrom` (read name into the by-name search, set/number as a form baseline).
- **"Copy to creation form"** (`copyFieldsToForm=true`) — the **escape hatch for a flaky search**:
  paste the read fields straight onto the form with NO catalog search, for the user to review/save.
  Both routes call `AddCardCopyPage::prefillFormFields` (species → `openAddCopyWithFields`,
  non-Pokémon → `startAddScannedCard(…, true)`); `prefillFormFields` writes only the printed-identity
  fields (leaving the default language pick intact) and touches the finder not at all.

Both hosts share a `pushAddPage`/`pushAddCardPage` helper (create + wire + run a prefill callback
before showing). Either way the deterministic catalog finder still produces the trustworthy printing
when used, and the LLM's reading is never the silent source of a saved copy's identity. New GUI dependency: `Qt6::Multimedia` +
`Qt6::MultimediaWidgets` (Ubuntu CI adds `qt6-multimedia-dev`); core stays Qt-free. **macOS camera
permission** is handled: `startCamera()` gates on
`qApp->checkPermission/requestPermission(QCameraPermission{})` and shows every state IN the screen
(waiting / denied-with-a-Settings-hint / no-camera), rather than the silent `qt.permissions` log
line users can't see. That whole block is **conditionally compiled**: `<QPermissions>` is a Qt
6.5+ header (Ubuntu CI's `qt6-base-dev` is 6.4 — an unguarded include is a hard `fatal error:
QPermissions: No such file or directory` there), and even on 6.5+ Qt only builds the API on the
platforms that HAVE such a gate. So the include is version-gated (`QT_VERSION_CHECK(6, 5, 0)`)
and the calls feature-gated behind the file-local `POKEDEX_HAS_CAMERA_PERMISSION`
(= `QT_CONFIG(permissions)`, which cannot itself be tested on 6.4 — an undefined
`QT_FEATURE_permissions` makes `QT_CONFIG` a preprocessor divide-by-zero, hence the two-step
guard); where neither holds there is nothing to ask for and the camera just opens. Any other
new Qt 6.5+ API needs the same treatment or a bump of CI's Qt. The `if(APPLE)` block does TWO
build-side things — BOTH required, missing
either silently auto-denies (no prompt, no Settings entry, just a log line): (1) the macOS build is
a real **`.app` bundle** (`MACOSX_BUNDLE` + `cmake/macos/Info.plist.in` → a `Contents/Info.plist`
with `NSCameraUsageDescription`, ad-hoc-signed with id `com.mazuh.pokedex-tcg`) — Qt's handler reads
the usage description from `[NSBundle mainBundle].infoDictionary` and TCC/LaunchServices register the
app by its bundle; a **bare binary does NOT work** (even with an embedded `__TEXT,__info_plist`
section: the request fails "Could not request QCameraPermission", `tccutil` can't find the id, the
app never appears in System Settings ▸ Camera — we tried and abandoned that path); (2)
**`qt_import_plugins(pokedex_tcg INCLUDE Qt6::QDarwinCameraPermissionPlugin)`** — brew's Qt is STATIC
so the permission plugin is a `.a` that must be explicitly imported (Qt doesn't auto-import them),
else `QCameraPermission` has no handler ("Could not find permission plugin"). Because it's a bundle,
the binary is `build/pokedex_tcg.app/Contents/MacOS/pokedex_tcg`: **dev.sh** runs that inner path
(bundle-recognized, logs still in the terminal), **install.sh**/CMake install the whole `.app` + a
`pokedex` symlink into it, README points at the `.app`. Reset the grant with `tccutil reset Camera
com.mazuh.pokedex-tcg` (after first launch). The captured frame is not persisted — see
`docs/ai-assistant.md`.
`gui/services/` holds `MediaService` (Pokémon artwork fetch+cache), `CardSearchService`
(card search transport — search results/thumbnails are **display/memory-only, never
cached to disk**; the lone exception is the small, near-static set table, which — when a
`CardSetCache` is injected — is loaded from disk on startup if younger than a 24h TTL
[skipping the daily-flaky `/v2/sets` fetch entirely on most launches], overwritten after a
fresh fetch, and loaded *stale* as a fallback when the fetch fails so narrowing survives an
API outage),
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
  file-in-binder) and appears both in the flat "My Cards" inventory and in the guide
  of the binder it is filed in — a binder is a physical object, so its guide accounts
  for every card in it — where, having no dex number to sort among the species, it
  lands after them. It is absent only from the Pokémon browser, which is indexed by
  species by construction. Storage
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

**A binder's physical layout: pages, pockets, and blanks.** A binder optionally records
what the album physically IS — its `capacity` (an int) and its `pocketGrid`
(`CardBinderPocketGrid{rows, columns}`) — both edited on `BinderEditPage` and shown as two
new sortable columns on `BindersPage`. The grid is modelled as ONE `std::optional<struct>`
rather than two independent `optional<int>`s precisely so "rows known, columns unknown" is
unrepresentable instead of merely invalid, sparing every reader a defensive branch (storage
still decodes a hand-edited half-set pair as unset). From it the binder guide derives its
first two columns — `Page` and `Pocket` ("row×column" from the page's top-left, e.g. "2×3")
— plus a separator drawn under the row that closes a page (`PageBreakDelegate`, reading a
`kPageEndRole` flag the fill loop sets). Formatting lives in `gui/views/binder_layout_labels.h`
(`pocketLabel`, `pocketGridLabel`, `percentLabel`), shared by both screens.

Three rules govern the pocket count, and each is load-bearing:
- **It counts filed-order position, never visible position.** The search box hides rows, and
  a surviving row must still name the page it is physically on; a visible-position number
  would renumber itself per search and send the user to the wrong sleeve. The cost is that
  the separators look arbitrary under a filter — accepted, the page number is the part that
  stays meaningful.
- **Everything holds a pocket EXCEPT a Removed copy.** Read `holdsPocket = !(copy &&
  isRemoved(*copy))` carefully: a placeholder row (a listed species, sleeve reserved) and a
  blank row (`copy == nullptr`) both DO hold one. Writing it the tempting other way round
  (`copy != nullptr && !isRemoved(*copy)`) makes blanks free and breaks the feature outright
  while every test still passes.
- **Page/Pocket blank out unless the grid is recorded AND the guide is in natural filed
  order** (`showPockets = perPage > 0 && sortColumn_ < 0`) — under a header sort the rows no
  longer follow the sleeves. The columns stay PRESENT but empty, never `setColumnHidden`:
  every column index in that file is a hardcoded literal, and the Page/Pocket headers are the
  only affordance that resets the sort back to natural order. Columns 0/1 must still be
  `setItem`-ed even when blank — the Removed-graying pass walks every column and would
  dereference a null cell. Use a plain empty item there, not `cell()`, whose em-dash means
  "this record has no data" rather than "this column doesn't apply".

**Blank pockets** (`CardBinderBlank`, table `card_binder_blank`) are the region-agnostic way
a user controls where a page breaks: species run contiguously by dex number, so Kanto ends
mid-page and Kalos's first evolution line splits across two pages — two blanks before Chespin
start Kalos at pocket 1×1. Collectors break pages by different rules, so the app stores where
they chose to leave gaps rather than guessing at one. A blank names EXACTLY ONE anchor and
sits immediately before that row: `beforeDexNum` (a species — the durable choice, and the one
the GUI mints for any row that has a species, since it survives that card being deleted and
re-added and works on a species not owned yet) or `beforeCopyId` (one exact card, for a
species-free row with no dex number to name it). `blanks` is a COUNT per anchor, not a row per
pocket — blanks at one anchor are indistinguishable, so a row-per-pocket table would need a
synthetic ordinal purely to permit duplicates. An ORPHANED blank (region un-scoped, copy
deleted or moved away) simply emits nothing; `buildEntries` stays read-only and never prunes
it, so restoring the region restores the layout. A blank guide row is `CardBinderEntry` with
BOTH optionals unset and `status == nullopt` — which is why `status` is now
`std::optional<CollectionStatus>` (an empty pocket has no collection verdict, and it composes
with `compareOptional` so blanks sink in the Status sort in both directions). `BinderView`'s
`showEntryInPanel` / `activateRow` branches that used to be defensive dead code are now the
real blank path.

The insert/remove affordance is ONE contextual button in a **new row under the table** —
"Insert blank", or "Remove blank" when the selected row is one. That placement follows the
precedent of every other list screen (`OwnedCardsView`, `BindersPage`, `WishlistView` all put
row actions below the table); the rule it settles for `BinderView` is **selection-scoped
actions go under the table, binder-scoped ones (Add a card, Edit binder, Refresh prices) stay
in the top bar**. It is disabled with an explaining tooltip when no grid is recorded or while
a header sort is active (that tooltip doubles as the hint for how to get back to filed order).
After a write the highlight returns to the ANCHOR row, not the blank — so pressing Insert
twice widens the same gap, which is exactly how a page gets padded out. Removing works off the
row the blank sits before (scan forward to the first row that anchors one), which is sound
only because the action is gated to filed order.

**A numeric field is a `QSpinBox`.** `BinderEditPage`'s capacity and rows×columns are the
first numeric inputs in the app. A spinbox rather than `QLineEdit` + `QIntValidator`: it makes
an invalid value unrepresentable (so `submit()` needs no parse branch), and
`setSpecialValueText` at the minimum (0) maps storage's own 0-is-unset sentinel onto a visible
"Not set" state — an empty line edit can't distinguish "not recorded" from "cleared". Do the
same for any new numeric field. The two grid sides are deliberately NOT auto-linked; a
half-set grid is caught by a message on submit (and by `BinderService`, the record of truth),
because silently coercing a value the user typed is worse than telling them.

**A card section re-reads on `showEvent`.** All three copy-backed sections —
`OwnedCardsView`, `PokemonListView`, and `BinderView` (the binder guide) — override
`showEvent` to re-load from storage when the section is (re-)shown, so a copy (or, for
the guide, a wishlist source) edited/moved/removed in another section isn't left stale
on tab return. `OwnedCardsView` and `BinderView` refresh unconditionally and rely on
their `repopulate()`'s by-identity selection restore to keep the user's place;
`PokemonListView` gates on `CardCopyService::revision()` because its ~1000-row rebuild is
expensive. Don't gate the binder guide on `revision()`: its Status column also derives
from the wishlist (which has no revision counter), and a revision stamp on a failed load
would latch an empty guide. The guide has no ctor load — the first `showEvent` does it.

**GUI navigation.** The app is a macOS-style shell: `MainWindow` has a left
sidebar (a `QListWidget` source list, Finder/Settings-style) selecting sections
in an outer `QStackedWidget`. `main.cpp` opens it with `showMaximized()` so the
list/detail splits have room without the user having to maximize first. Prefer navigating *within* the window — swap pages
in a `QStackedWidget` (as `BindersPage` does: binder table ⇄ binder guide, with
a Back button) — over opening a second top-level window. A separate window or
modal dialog is a deliberate, rare exception (the first-run setup, the
`BinderPickerDialog`, and the About box), not the default for showing more
detail. **Prefer a dedicated screen (or, more rarely, an inline form) over a modal
for CRUD.** Creating/editing a record is done on a full page pushed onto the host's
`QStackedWidget` with a Back top bar — not a `QDialog` or a `QInputDialog`. Binder
create/edit is the canonical example: the reusable `BinderEditPage`
(`gui/views/binder_edit_page`, create + edit modes over `BinderService`; a name field
plus a **checkbox per region** — the region set is multivalued) is pushed
by **both** hosts — `BindersPage` (its "New…" / "Edit…" buttons) and `BinderView`
(an "Edit binder" button beside "Refresh prices" in the guide's top bar) — and on
Back each host re-reads the binder(s) from storage (`BinderView` also re-sets its
heading and recomputes the guide, since a region change alters the species list).
This replaced the old `BinderEditorDialog` modal and the rename `QInputDialog`. The one menu bar lives on `MainWindow` (attached via `layout->setMenuBar`,
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

**A multi-figure status line that needs per-figure tooltips is several small
`QLabel`s, not one rich-text label.** Qt has no per-span tooltip inside a label, and
faking one with `<a>` + `linkHovered` + `QToolTip::showText` turns statistics into
fake links (wrong affordance) while an `event()` override mapping cursor-x through
`QFontMetrics` is fragile under translation and DPI. So put one `QLabel` per figure in
a zero-margin, zero-spacing `QHBoxLayout` with a trailing `addStretch()`, declare the
muted colour ONCE as a `QLabel { color: gray; }` stylesheet on the container (it
cascades to the children), set each `setToolTip` in the ctor (the text is static), and
bake the `" · "` separator into the **leading** edge of every label but the first — so
hiding a *trailing* stat takes its separator with it. That trick is free only for trailing
labels: hiding a LEADING one still needs an explicit fixup, since the label that becomes
first must drop its separator (`BinderView`'s Listed / Captured / Cards / value line is the
reference — it hides Listed and Captured together when the binder lists no species, and
switches Cards to a separator-less variant for exactly that case).

**A form's primary/submit button gets the shared accent affordance.** Every
in-window CRUD form's commit button — "Add copy" (`AddCardCopyPage`), "Save
changes" (`EditCardCopyPage`), "Create binder"/"Save changes" (`BinderEditPage`),
"Add" (`WishlistSourcesEditor`) — is routed through
`applyPrimaryButtonStyle` (`gui/views/primary_button.h`) so the user can spot the
button that commits the form at a glance instead of scanning a row of look-alike
grey buttons. The helper paints the OS accent colour (from the palette's
`Highlight`/`HighlightedText`, so it follows the system accent and the light/dark
theme) plus a confirm "✓" icon, with all four states (normal/hover/pressed/disabled)
spelled out — a stylesheet rather than a palette because a native macOS
`QPushButton` ignores a palette background. Do the same for any **new** form's submit
button (accepts any `QAbstractButton`; pass `withIcon=false` for a default icon-only
`QToolButton`, where an icon would replace the text). It marks exactly ONE button per
form — the commit action — never the secondary actions beside it (Upload a photo,
"Same set as last…", Back), so the accent stays a reliable "this is the primary
action" signal.

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

**A large table's row rebuild wraps the fill in `BulkTablePopulate`.** With a
column in `QHeaderView::ResizeToContents`, EVERY `setItem` re-measures that column
across all rows, so refilling an already-populated, visible table is O(rows²) — a
multi-second UI freeze on a big list (the binder guide's ~1000 rows was the symptom:
first open cheap because the scans grow from empty, but a tab-return `repopulate()`
over the full table hung for seconds). Wrap the `setRowCount` + `setItem` loop in a
`BulkTablePopulate` guard (`gui/views/table_bulk_update.h`): it saves each content
column's resize mode, drops them to `Interactive` and freezes repaints during the
fill, then replays the saved modes for a single trailing measurement pass — turning
tens of seconds into milliseconds. Name the content-sized column set once as a
`kAutoFitColumns` constant shared between the ctor's resize-mode setup and the guard
so the two can't drift (a `Stretch`/manually-`Interactive` column is left out — it
doesn't rescan). `restore()` is idempotent: call it explicitly when the auto-fit must
run before a later same-method pass (e.g. `OwnedCardsView` measures widths before its
filter/selection pass), with the destructor as the exception backstop. `BinderView`
and `OwnedCardsView` use it; do the same for any new table that can grow large.

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
set-vs-unset case for the shell's flip (see `compareOptional` in
`gui/views/sortable_table.h`, used for the Condition/Rarity/Foil/Prices columns).

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
