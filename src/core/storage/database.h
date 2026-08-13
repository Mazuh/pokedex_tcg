#pragma once

#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>

// Forward-declared so sqlite3.h never leaks through this header: SQLite stays a
// private implementation detail of pokedex_core.
struct sqlite3;

namespace pokedex {

// STORAGE — raised on any SQLite failure (open, exec, migration).
class StorageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// STORAGE — RAII owner of a single SQLite connection. Opening turns on foreign
// keys; migrate() brings the schema up to kSchemaVersion. The sqlite3 handle is
// never exposed, so callers cannot depend on SQLite types.
class Database {
public:
    // Schema version this build produces. Bump it and extend migrate() when the
    // schema changes; migrations are additive.
    //   v1 — initial schema (binders, copies, wishlist).
    //   v2 — card_copy.ref_set_name (the human set name, for code-less sets).
    //   v3 — card_copy.ref_name (the printed card name; the only title a
    //        species-free Trainer/Energy card has).
    //   v4 — card_copy.rarity (the card's rarity classification; optional).
    //   v5 — card_copy.foil (the card's foil treatment / finish; optional).
    //   v6 — card_set_cache + cache_meta (a TTL'd local cache of the external
    //        /v2/sets table; reference data, not collection source-of-truth).
    //   v7 — card_price + card_price_fetch (on-demand cache of a card's market
    //        prices, keyed by external card id; also reference data).
    //   v8 — card_copy.external_card_id (links a copy to its external catalog card
    //        so its prices can be looked up; optional/blank for unlinked copies).
    //   v9 — card_set_cache keyed by (source, id) so a second provider (tcgdex)
    //        caches its set table beside pokemontcg's; old rows re-tagged 'pokemontcg'.
    //   v10 — card_price_suppression (external_card_id, provenance): a per-card, per-vendor
    //        "hide this vendor's price" that survives a Refresh and is dropped only by Clear.
    //   v11 — card_binder_region (binder_id, region): a binder's region scope becomes
    //        MULTIVALUED (a "Kanto + Kalos" album). The v1 card_binder.region column is left
    //        vestigial, its value backfilled into the join table.
    //   v12 — card_binder.capacity / pocket_rows / pocket_columns: the album's optional
    //        physical layout, 0 = unset. Capacity is stored, not derived from the grid.
    //   v13 — card_binder_blank (binder_id, before_dex_num, before_copy_id): deliberate empty
    //        pockets, the user-driven page-break mechanism. `blanks` is a count per anchor.
    //   v14 — card_binder_placement (binder_id, card_copy_id): a card pulled OUT of the
    //        guide's derived order and pinned immediately before another row — the "this
    //        goes at page 18, pocket 2×2" gesture. Anchors to a copy by preference (both
    //        anchors unset = at the very end); `ordinal` orders same-anchor placements.
    //
    // NOTE: from v12 on, a step ALTERs a v1 table, so "migrate a fresh DB, then roll the
    // stamp back and re-migrate" is no longer a safe way to exercise a step in a test — the
    // replayed ADD COLUMN fails as a duplicate. Stand the older shape up by hand instead.
    static constexpr int kSchemaVersion = 14;

    // Open (creating if absent) the database at `path`, or an in-memory database
    // when path == ":memory:". Throws StorageError on failure.
    explicit Database(const std::filesystem::path& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;
    Database(Database&&) = delete;
    Database& operator=(Database&&) = delete;

    // Execute one or more statements that yield no rows (DDL, PRAGMA writes).
    void exec(const std::string& sql);

    // Run `body` inside a BEGIN/COMMIT transaction, rolling back (best-effort) and
    // re-throwing if it throws. The single home for the multi-statement-write guard
    // that every repository needs — a mid-sequence failure must never leave a
    // committed partial write. Not reentrant (SQLite has no nested transactions):
    // never call from inside another transaction() body.
    void transaction(const std::function<void()>& body);

    // PRAGMA user_version accessors — the schema version stamped in the file.
    int userVersion();
    void setUserVersion(int version);

    // Create/upgrade the schema to kSchemaVersion inside a transaction.
    // Idempotent: a no-op when the database is already current.
    void migrate();

    // Write a consistent snapshot of this database to `destination`, using SQLite's
    // VACUUM INTO — an atomic read-transaction copy of the LIVE file. A backup must
    // never byte-copy pokedex.db while it is open: a page can change mid-read and a
    // rollback journal may sit beside it, so the copy can be torn.
    //
    // `destination` must NOT already exist (SQLite refuses to overwrite, which is a
    // free "never clobber an existing backup" guard) and its parent directory must.
    // Must NOT be called from inside transaction(): VACUUM cannot run in an open
    // transaction. Throws StorageError on any failure.
    //
    // Two properties worth knowing, both relied on by the backup feature:
    //   - user_version IS preserved, so a restored snapshot is not silently
    //     re-migrated (tests/storage/database_test.cpp pins this).
    //   - VACUUM reassigns rowids for tables without an INTEGER PRIMARY KEY, and
    //     card_copy is one (the binder guide orders extras by `inserted_at, rowid`).
    //     That is safe: VACUUM copies in source-rowid order, so relative order — the
    //     only thing that ordering depends on — survives.
    //
    // Requires SQLite >= 3.27 (2019); macOS ships 3.51 and ubuntu-latest 3.45.
    void backupTo(const std::filesystem::path& destination);

    // Rows changed by the most recently completed INSERT/UPDATE/DELETE on this
    // connection (sqlite3_changes). Lets a repository tell "updated a row" from
    // "matched nothing".
    int changes();

    // STORAGE-INTERNAL: the raw connection, for repositories and Statement to
    // prepare bound queries. The type stays forward-declared, so callers still
    // cannot depend on SQLite types through this header; only storage-layer .cpp
    // files that include <sqlite3.h> can do anything with the returned pointer.
    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;
};

}  // namespace pokedex
