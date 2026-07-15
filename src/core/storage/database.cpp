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

void Database::setUserVersion(int version) {
    // PRAGMA does not accept bound parameters; version is an int we control.
    exec("PRAGMA user_version = " + std::to_string(version) + ";");
}

void Database::migrate() {
    if (userVersion() >= kSchemaVersion) {
        return;
    }
    exec("BEGIN;");
    try {
        exec(kSchemaV1);
        setUserVersion(kSchemaVersion);
        exec("COMMIT;");
    } catch (...) {
        // Best-effort rollback so a failed migration leaves no half-built schema.
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}

}  // namespace pokedex
