#pragma once

#include <optional>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/domain/types.h"

namespace pokedex {

class Database;

// APP — the local persistence of the external card-catalog set table (the
// pokemontcg.io /v2/sets projection, a vector<CardSetInfo>). It lives in app/,
// not storage/, because its row type CardSetInfo is an app-layer projection of
// external data: a storage-layer repository referencing it would invert the
// layering (storage must not depend on app). app/ is allowed to use storage/, so
// this accessor owns the SQL against the card_set_cache / cache_meta tables that
// Database::migrate() creates (schema v6).
//
// Why persist reference data at all (the search transport otherwise caches
// nothing to disk): the set table is small (~170 rows) and near-static, but the
// public API is daily-flaky and 500s the /v2/sets fetch. Caching it lets the app
// skip that fetch on most launches (a TTL the caller enforces via fetchedAt) and
// still narrow searches from the last good copy when the API is down. This is the
// ONLY external data cached to SQLite; search results / thumbnails remain
// memory-only. SQLite is the app's sole persistence format, so the cache lives
// here rather than as a JSON sidecar.
class CardSetCache {
public:
    // `db` must outlive this accessor (like every repository).
    explicit CardSetCache(Database& db) : db_(db) {}

    // Every cached set, ordered by id. Empty when nothing has been stored.
    std::vector<CardSetInfo> load();

    // When the cached table was last written, or nullopt if never — the caller
    // compares it against "now" to decide whether the cache is still fresh.
    std::optional<Timestamp> fetchedAt();

    // Replace the whole cached table with `sets` and stamp `fetchedAt`, in one
    // transaction (a partial write would leave a table that disagrees with its
    // timestamp). An empty `sets` clears the table but still records the stamp.
    void store(const std::vector<CardSetInfo>& sets, Timestamp fetchedAt);

private:
    Database& db_;
};

}  // namespace pokedex
