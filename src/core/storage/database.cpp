#include "core/storage/database.h"

#include <sqlite3.h>

#include <string>

namespace pokedex {

namespace {

// The v1 schema. Only the Collection zone (the user's source of truth) is
// stored; catalog data (Region/Pokemon) is compile-time, and inferred types are
// recomputed. Timestamps are ISO-8601 UTC TEXT and enums are stable text tokens
// (see docs/CLAUDE.md) — the code that encodes them ships with the repositories.
constexpr char kSchemaV1[] = R"sql(
CREATE TABLE card_binder (
  id           TEXT PRIMARY KEY,
  name         TEXT NOT NULL,
  region       TEXT,
  inserted_at  TEXT NOT NULL,
  updated_at   TEXT NOT NULL
);

CREATE TABLE card_copy (
  id               TEXT PRIMARY KEY,
  pokemon_dex_num  INTEGER NOT NULL,
  ref_expansion    TEXT NOT NULL,
  ref_language     TEXT NOT NULL,
  ref_collector    TEXT NOT NULL,
  ownership        TEXT NOT NULL,
  condition        TEXT NOT NULL,
  binder_id        TEXT REFERENCES card_binder(id) ON DELETE SET NULL,
  comments         TEXT NOT NULL DEFAULT '',
  inserted_at      TEXT NOT NULL,
  updated_at       TEXT NOT NULL
);
CREATE INDEX idx_card_copy_binder  ON card_copy(binder_id);
CREATE INDEX idx_card_copy_pokemon ON card_copy(pokemon_dex_num);

CREATE TABLE wishlist (
  pokemon_dex_num  INTEGER PRIMARY KEY,
  inserted_at      TEXT NOT NULL,
  updated_at       TEXT NOT NULL
);

CREATE TABLE wishlist_source (
  pokemon_dex_num  INTEGER NOT NULL REFERENCES wishlist(pokemon_dex_num) ON DELETE CASCADE,
  source           TEXT NOT NULL,
  PRIMARY KEY (pokemon_dex_num, source)
);
)sql";

// v1 → v2: record the human set name alongside the printed code. Optional (blank
// default), because a code-less set has no printed code and reuses collector
// numbers across years, so the name is the only disambiguator.
constexpr char kMigrationV2[] =
    "ALTER TABLE card_copy ADD COLUMN ref_set_name TEXT NOT NULL DEFAULT '';";

// v2 → v3: record the printed card name. A species card can derive a title from
// its dex number, but a species-free card (Trainer/Energy) has none — this is
// its only human-readable label. Optional (blank default) so existing rows and
// hand-entered copies are valid without it.
constexpr char kMigrationV3[] =
    "ALTER TABLE card_copy ADD COLUMN ref_name TEXT NOT NULL DEFAULT '';";

// v3 → v4: record the card's rarity classification (Common … Hyper Rare, Promo,
// plus the legacy rarities). Optional (blank default), like condition — the empty
// string is the codec's nullopt sentinel, so existing rows decode to "no rarity".
constexpr char kMigrationV4[] =
    "ALTER TABLE card_copy ADD COLUMN rarity TEXT NOT NULL DEFAULT '';";

// v4 → v5: record the card's foil treatment / finish (Non-Holo, Holo, Reverse
// Holo…). Optional (blank default), independent of rarity; existing rows decode to
// "no foil".
constexpr char kMigrationV5[] =
    "ALTER TABLE card_copy ADD COLUMN foil TEXT NOT NULL DEFAULT '';";

// v5 → v6: a local cache of the external card-catalog set table (pokemontcg.io
// /v2/sets). This is NOT collection source-of-truth — it is external reference
// data — but SQLite is this app's sole persistence format, so the cache lives here
// rather than as a JSON sidecar. It lets the app skip the daily-flaky /v2/sets
// fetch on most launches and still narrow searches when the API is down. The
// generic cache_meta(key,value) table records when the set table was last fetched
// (and is reusable for any future TTL'd cache). See core/app/card_set_cache.
constexpr char kMigrationV6[] = R"sql(
CREATE TABLE card_set_cache (
  id             TEXT PRIMARY KEY,
  ptcgo_code     TEXT NOT NULL,
  name           TEXT NOT NULL,
  printed_total  INTEGER NOT NULL
);

CREATE TABLE cache_meta (
  key    TEXT PRIMARY KEY,
  value  TEXT NOT NULL
);
)sql";

// v6 → v7: the on-demand cache of a card's market prices (pokemontcg.io per-card
// tcgplayer/cardmarket blocks), plus manually-entered prices. Like the set cache
// this is NOT collection source-of-truth — a card's price is keyed by
// `external_card_id`, a source-neutral card identity (today always a pokemontcg.io
// card id, e.g. "sv3-125", the only source wired up — the name stays provider-
// agnostic so another catalog can file prices here without a schema rename), NOT by
// any card_copy, so deleting a copy never removes pricing and one card can carry
// many prices at once (several vendors × variants × metrics; there is no single true
// price). card_price_fetch records when WE last
// hit the API for a given card so the caller can enforce a TTL and avoid hammering
// the free API. Each card_price row is one observation: `provenance` is the source
// ('tcgplayer'/'cardmarket'/'manual'), `variant`/`metric` locate it within a vendor
// (e.g. holofoil/market), `amount_cents` is integer minor units (no float drift),
// and `observed_at` is when the source last updated that price. See
// core/app/card_price_cache. A refetch replaces only the API-sourced rows for a
// card (provenance != 'manual'); manual rows survive. See core/app/card_price_cache.
constexpr char kMigrationV7[] = R"sql(
CREATE TABLE card_price_fetch (
  external_card_id    TEXT PRIMARY KEY,
  fetched_at  TEXT NOT NULL
);

CREATE TABLE card_price (
  id            TEXT PRIMARY KEY,
  external_card_id      TEXT NOT NULL,
  provenance    TEXT NOT NULL,
  variant       TEXT NOT NULL DEFAULT '',
  metric        TEXT NOT NULL DEFAULT '',
  amount_cents  INTEGER NOT NULL,
  currency      TEXT NOT NULL,
  observed_at   TEXT NOT NULL,
  note          TEXT NOT NULL DEFAULT ''
);
CREATE INDEX idx_card_price_external_id ON card_price(external_card_id);
)sql";

// v7 → v8: link a copy to its external catalog card. `external_card_id` is the same
// source-neutral id the price cache keys on (today a pokemontcg.io card id like
// "sv3-125"), so an owned copy can look up its prices. Optional (blank default): a
// hand-entered copy, or one added before this column existed, is simply unlinked —
// no price lookup until a card id is attached.
constexpr char kMigrationV8[] =
    "ALTER TABLE card_copy ADD COLUMN external_card_id TEXT NOT NULL DEFAULT '';";

// v8 → v9: make the set cache serve MORE THAN ONE provider from one table. The catalog
// (pokemontcg.io) and the pricing provider (tcgdex) each publish their own set list with
// their own id scheme ("sv3" vs "sv03"), so a set is stored once per provider — a `source`
// discriminator, part of the primary key so the two providers' ids can't collide. Both go
// through the single CardSetCache class (scoped by source), rather than a second bespoke
// per-vendor cache. Per-source TTL lives in cache_meta under key "sets_fetched_at:<source>".
//
// The existing rows were all pokemontcg's, so they are PRESERVED (re-tagged source
// 'pokemontcg') rather than dropped — the set cache exists precisely so set-narrowing
// survives an outage of the daily-flaky /v2/sets, and dropping it would strand the very
// first post-upgrade launch with no stale fallback if that API were down. The old single
// fetch stamp is renamed to the per-source key so the preserved rows keep their real age.
constexpr char kMigrationV9[] = R"sql(
ALTER TABLE card_set_cache RENAME TO card_set_cache_v8;

CREATE TABLE card_set_cache (
  source         TEXT NOT NULL,
  id             TEXT NOT NULL,
  ptcgo_code     TEXT NOT NULL,
  name           TEXT NOT NULL,
  printed_total  INTEGER NOT NULL,
  PRIMARY KEY (source, id)
);

INSERT INTO card_set_cache(source, id, ptcgo_code, name, printed_total)
  SELECT 'pokemontcg', id, ptcgo_code, name, printed_total FROM card_set_cache_v8;

DROP TABLE card_set_cache_v8;

UPDATE cache_meta SET key = 'sets_fetched_at:pokemontcg' WHERE key = 'sets_fetched_at';
)sql";

// v9 → v10: per-card vendor suppressions — a row (external_card_id, provenance) means "hide
// this vendor's price for this card" (e.g. tcgplayer has no good match for a non-holo copy).
// Kept SEPARATE from the price cache on purpose: a Refresh rewrites card_price but never
// touches this table, so a suppression survives Refresh; Clear deletes it (CardPriceCache::
// clear) so the card goes back to accepting the API as-is. Keyed by the card (like prices),
// not the copy.
constexpr char kMigrationV10[] = R"sql(
CREATE TABLE card_price_suppression (
  external_card_id  TEXT NOT NULL,
  provenance        TEXT NOT NULL,
  PRIMARY KEY (external_card_id, provenance)
);
)sql";

// v10 → v11: a binder can now be scoped to MORE THAN ONE region, so the single
// card_binder.region column becomes a card_binder_region join table (one row per
// binder×region), mirroring wishlist_source. The existing single-region rows are
// backfilled into it (blank/NULL regions contribute nothing), then the old column
// is left in place — vestigial, never read again — since additive migrations don't
// rewrite v1's table. ON DELETE CASCADE drops a removed binder's region rows.
constexpr char kMigrationV11[] = R"sql(
CREATE TABLE card_binder_region (
  binder_id  TEXT NOT NULL REFERENCES card_binder(id) ON DELETE CASCADE,
  region     TEXT NOT NULL,
  PRIMARY KEY (binder_id, region)
);
INSERT INTO card_binder_region(binder_id, region)
  SELECT id, region FROM card_binder WHERE region IS NOT NULL AND region <> '';
)sql";

// v11 → v12: a binder's optional PHYSICAL LAYOUT — how many cards the album holds
// (capacity) and the shape of one page (pocket_rows × pocket_columns), which is what
// lets the guide say a card sits on page 18, pocket 1×3. All three use the 0 = unset
// sentinel (the same convention as pokemon_dex_num's 0 and condition's ""), so every
// existing binder backfills to "unset" — DEFAULT 0 *is* the backfill — with no
// nullable-column table rebuild.
//
// Capacity is stored INDEPENDENTLY of the grid, not derived as pages × pocketsPerPage:
// it is a fact about the album ("a 360-card binder") and the page count isn't recorded.
// It is never enforced — a stuffed binder is a real thing, so the guide displays how
// full it is and never blocks a card.
constexpr char kMigrationV12[] = R"sql(
ALTER TABLE card_binder ADD COLUMN capacity       INTEGER NOT NULL DEFAULT 0;
ALTER TABLE card_binder ADD COLUMN pocket_rows    INTEGER NOT NULL DEFAULT 0;
ALTER TABLE card_binder ADD COLUMN pocket_columns INTEGER NOT NULL DEFAULT 0;
)sql";

// v12 → v13: deliberate EMPTY POCKETS — per binder, "leave N pockets blank immediately
// before this row". This is the region-agnostic way a user controls where a page breaks:
// species run contiguously by dex number, so Kanto ends mid-page and Kalos's first
// evolution line gets split across two pages; two blanks before Chespin start Kalos at
// pocket 1×1 instead. Collectors break pages by different rules, so the app stores where
// they chose to leave gaps rather than guessing at one.
//
// Mirrors card_binder_region: a multivalued child of the binder, ON DELETE CASCADE. The
// anchor is EITHER a species (before_dex_num — durable, it survives the card being deleted
// and re-added, and works on a species not owned yet) OR one exact filed card
// (before_copy_id, for a species-free card with no dex number to name it); the unused half
// holds the 0 / '' unset sentinel and both are in the primary key, so a binder keeps one
// blank RUN per anchor.
//
// `blanks` is a COUNT rather than one row per pocket: blanks at one anchor are
// indistinguishable, so a row-per-pocket table would need a synthetic ordinal in the key
// purely to permit duplicates. before_copy_id carries NO foreign key — '' is a legal value
// here and would violate one — so a hard-deleted anchor copy leaves an inert row that the
// guide simply never emits.
constexpr char kMigrationV13[] = R"sql(
CREATE TABLE card_binder_blank (
  binder_id       TEXT    NOT NULL REFERENCES card_binder(id) ON DELETE CASCADE,
  before_dex_num  INTEGER NOT NULL DEFAULT 0,
  before_copy_id  TEXT    NOT NULL DEFAULT '',
  blanks          INTEGER NOT NULL,
  PRIMARY KEY (binder_id, before_dex_num, before_copy_id)
);
)sql";

// v13 → v14: MOVED CARDS — per binder, "this card sits immediately before that row".
// The guide's order is otherwise wholly derived (species by dex, a species' copies in
// filed order, species-free cards last), which leaves no way to express the gesture a
// collector arranging a real album makes: "this one goes at page 18, pocket 2×2".
//
// The page/pocket coordinates are NOT stored. They are a rendering of the resolved
// order, so a stored coordinate would go stale the moment anything before it changed;
// an anchor survives every such edit. That is also what lets the GUI treat coordinates
// as a pure translation layer on top and disable them entirely for a binder that never
// recorded a pocket grid.
//
// Anchoring mirrors card_binder_blank with two deliberate differences. It anchors to a
// COPY by preference (before_copy_id) rather than a species, because a move is about one
// exact sleeve and must be able to land between the second and third copy of a species;
// before_dex_num covers only the placeholder-row case, which has no card to name. And
// BOTH halves unset is legal here, meaning "at the very end" — without it the last
// pocket would be unreachable, since every other target is expressed as "before X".
//
// card_copy_id, unlike before_copy_id, is never '' — so it CAN carry a foreign key, and
// does: a hard-deleted copy takes its placement with it rather than leaving an inert row.
// Blanks anchored to that copy still orphan, which is the accepted, tested behaviour.
//
// `ordinal` orders the placements sharing one anchor, ascending and emitted nearest-last,
// so a card aimed at the anchor row's own pocket appends (max + 1) and nothing is ever
// renumbered. A placement may anchor to a copy that is itself placed — that is how you
// target a pocket a moved card already holds — so placements chain; the guide resolves
// that with a fixed-point pass and drops any placement whose anchor never resolves,
// leaving the copy in its natural position rather than making it invisible.
constexpr char kMigrationV14[] = R"sql(
CREATE TABLE card_binder_placement (
  binder_id       TEXT    NOT NULL REFERENCES card_binder(id) ON DELETE CASCADE,
  card_copy_id    TEXT    NOT NULL REFERENCES card_copy(id)   ON DELETE CASCADE,
  before_dex_num  INTEGER NOT NULL DEFAULT 0,
  before_copy_id  TEXT    NOT NULL DEFAULT '',
  ordinal         INTEGER NOT NULL DEFAULT 0,
  PRIMARY KEY (binder_id, card_copy_id)
);
)sql";

// v14 → v15: CARDS WITH NO FIXED POSITION — a copy filed in a binder that keeps no home
// sleeve, because the user rearranges it on demand (duplicates, trade fodder, a Trainer
// card that moves around). It is the exact opposite of v14's placement: a placement pins a
// card to ONE pocket, and these cards want none, so neither the Pokédex checklist nor the
// blank/placement arrangement machinery should account for them. The guide lists them in a
// loose run after everything else.
//
// It rides on card_copy rather than a third binder-scoped table because filing already
// does (card_copy.binder_id): a copy sits in at most one binder, so a per-copy flag needs
// no key of its own, and a card the user treats as loose stays loose if it is refiled.
// 0 = it takes its derived place, so DEFAULT 0 IS the backfill for every existing copy.
constexpr char kMigrationV15[] =
    "ALTER TABLE card_copy ADD COLUMN no_fixed_position INTEGER NOT NULL DEFAULT 0;";

}  // namespace

Database::Database(const std::filesystem::path& path) {
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    if (sqlite3_open_v2(path.string().c_str(), &db_, flags, nullptr) != SQLITE_OK) {
        StorageError err(db_ != nullptr ? sqlite3_errmsg(db_) : "cannot open database");
        sqlite3_close(db_);
        db_ = nullptr;
        throw err;
    }
    // The connection is open; if configuring it throws, the constructor never
    // completes so ~Database() won't run — close the handle ourselves first.
    try {
        exec("PRAGMA foreign_keys = ON;");
    } catch (...) {
        sqlite3_close(db_);
        db_ = nullptr;
        throw;
    }
}

Database::~Database() { sqlite3_close(db_); }

void Database::exec(const std::string& sql) {
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errmsg) != SQLITE_OK) {
        StorageError err(errmsg != nullptr ? errmsg : "sql execution failed");
        sqlite3_free(errmsg);
        throw err;
    }
}

int Database::userVersion() {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA user_version;", -1, &stmt, nullptr) != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(db_));
    }
    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

void Database::backupTo(const std::filesystem::path& destination) {
    // The destination is bound, never interpolated: a workspace path can hold quotes,
    // and VACUUM INTO does accept a bound parameter (unlike PRAGMA above).
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "VACUUM main INTO ?1;", -1, &stmt, nullptr) != SQLITE_OK) {
        throw StorageError(sqlite3_errmsg(db_));
    }
    const std::string path = destination.string();
    if (sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        StorageError err(sqlite3_errmsg(db_));
        sqlite3_finalize(stmt);
        throw err;
    }
    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        // Capture the message before finalize, which can reset it. The most common
        // failures are "output file already exists" and an unwritable directory.
        StorageError err(std::string(sqlite3_errmsg(db_)) + " (" + path + ")");
        sqlite3_finalize(stmt);
        throw err;
    }
    sqlite3_finalize(stmt);
}

int Database::changes() { return sqlite3_changes(db_); }

void Database::setUserVersion(int version) {
    // PRAGMA does not accept bound parameters; version is an int we control.
    exec("PRAGMA user_version = " + std::to_string(version) + ";");
}

void Database::transaction(const std::function<void()>& body) {
    exec("BEGIN;");
    try {
        body();
        // COMMIT is inside the try so that a failing COMMIT (e.g. SQLITE_BUSY from a
        // concurrent holder, or disk-full) also runs the ROLLBACK below — otherwise the
        // BEGIN transaction would stay open on the connection and poison later writes.
        exec("COMMIT;");
    } catch (...) {
        // Best-effort rollback so a failed body OR commit leaves no half-applied write
        // and a clean connection; the rollback's own failure must not mask the original.
        try {
            exec("ROLLBACK;");
        } catch (...) {
            // The transaction is already doomed; surface the original failure.
        }
        throw;
    }
}

void Database::migrate() {
    const int from = userVersion();
    if (from >= kSchemaVersion) {
        return;
    }
    // Apply each step whose target version is newer than the file's, in order, so a
    // fresh database (v0) runs the whole chain and an existing one only the tail. The
    // whole chain runs in one transaction so a failed migration leaves no half-built
    // schema.
    transaction([&] {
        if (from < 1) {
            exec(kSchemaV1);
        }
        if (from < 2) {
            exec(kMigrationV2);
        }
        if (from < 3) {
            exec(kMigrationV3);
        }
        if (from < 4) {
            exec(kMigrationV4);
        }
        if (from < 5) {
            exec(kMigrationV5);
        }
        if (from < 6) {
            exec(kMigrationV6);
        }
        if (from < 7) {
            exec(kMigrationV7);
        }
        if (from < 8) {
            exec(kMigrationV8);
        }
        if (from < 9) {
            exec(kMigrationV9);
        }
        if (from < 10) {
            exec(kMigrationV10);
        }
        if (from < 11) {
            exec(kMigrationV11);
        }
        if (from < 12) {
            exec(kMigrationV12);
        }
        if (from < 13) {
            exec(kMigrationV13);
        }
        if (from < 14) {
            exec(kMigrationV14);
        }
        if (from < 15) {
            exec(kMigrationV15);
        }
        setUserVersion(kSchemaVersion);
    });
}

}  // namespace pokedex
