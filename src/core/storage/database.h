#pragma once

#include <filesystem>
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
    static constexpr int kSchemaVersion = 6;

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

    // PRAGMA user_version accessors — the schema version stamped in the file.
    int userVersion();
    void setUserVersion(int version);

    // Create/upgrade the schema to kSchemaVersion inside a transaction.
    // Idempotent: a no-op when the database is already current.
    void migrate();

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
