#pragma once

#include <optional>
#include <string>
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
// Database::migrate() creates (the multi-provider shape since schema v9).
//
// One cache, MANY providers. The metadata catalog (pokemontcg.io) and the pricing
// provider (tcgdex) each publish their own set list under their own id scheme, so a
// CardSetCache is scoped to ONE `source` at construction ("pokemontcg" / "tcgdex"): every
// read and write is filtered to that source, and rows for different sources coexist in the
// one table (keyed by (source, id)). This keeps the storage a single table + class rather
// than a bespoke cache per vendor — the two providers just pick different sources.
//
// Why persist reference data at all (the search/price transports otherwise cache
// nothing to disk): a set table is small (~170–220 rows) and near-static, but the
// public APIs are daily-flaky. Caching lets the app skip the /sets fetch on most launches
// (a TTL the caller enforces via fetchedAt) and still work from the last good copy when the
// API is down. This is the ONLY external data cached to SQLite; search results / thumbnails
// / per-card prices' transport stay memory-or-on-demand. SQLite is the app's sole
// persistence format, so the cache lives here rather than as a JSON sidecar.
class CardSetCache {
public:
    // `db` must outlive this accessor (like every repository). `source` scopes every read
    // and write (e.g. "pokemontcg", "tcgdex") so multiple providers share the one table.
    CardSetCache(Database& db, std::string source) : db_(db), source_(std::move(source)) {}

    // Every cached set for this source, ordered by id. Empty when nothing has been stored.
    std::vector<CardSetInfo> load();

    // When this source's cached table was last written, or nullopt if never — the caller
    // compares it against "now" to decide whether the cache is still fresh.
    std::optional<Timestamp> fetchedAt();

    // Replace this source's cached rows with `sets` and stamp `fetchedAt`, in one
    // transaction (a partial write would leave rows that disagree with the timestamp).
    // Only this source's rows are touched; other providers' rows are left intact. An empty
    // `sets` clears this source but still records the stamp.
    void store(const std::vector<CardSetInfo>& sets, Timestamp fetchedAt);

private:
    Database& db_;
    std::string source_;
};

}  // namespace pokedex
