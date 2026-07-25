#include "core/app/card_set_cache.h"

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {
// The one cache_meta key this cache owns: the ISO-8601 stamp of the last set fetch.
constexpr char kFetchedAtKey[] = "sets_fetched_at";
}  // namespace

std::vector<CardSetInfo> CardSetCache::load() {
    Statement stmt(db_,
                   "SELECT id, ptcgo_code, name, printed_total"
                   " FROM card_set_cache ORDER BY id;");
    std::vector<CardSetInfo> sets;
    while (stmt.step()) {
        CardSetInfo info;
        info.id = stmt.columnText(0);
        info.ptcgoCode = stmt.columnText(1);
        info.name = stmt.columnText(2);
        info.printedTotal = stmt.columnInt(3);
        sets.push_back(std::move(info));
    }
    return sets;
}

std::optional<Timestamp> CardSetCache::fetchedAt() {
    Statement stmt(db_, "SELECT value FROM cache_meta WHERE key = ?;");
    stmt.bindText(1, kFetchedAtKey);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return timestampFromIso(stmt.columnText(0));
}

void CardSetCache::store(const std::vector<CardSetInfo>& sets, Timestamp fetchedAt) {
    // The row replacement and its timestamp are one logical unit spread across
    // several statements, so they run in a transaction with a best-effort ROLLBACK
    // — mirroring WishlistRepository::save and migrate(). Otherwise a mid-write
    // failure could leave rows that disagree with the recorded fetch time.
    db_.exec("BEGIN;");
    try {
        Statement clear(db_, "DELETE FROM card_set_cache;");
        clear.step();

        for (const CardSetInfo& set : sets) {
            Statement ins(db_,
                          "INSERT INTO card_set_cache(id, ptcgo_code, name, printed_total)"
                          " VALUES(?, ?, ?, ?);");
            ins.bindText(1, set.id);
            ins.bindText(2, set.ptcgoCode);
            ins.bindText(3, set.name);
            ins.bindInt(4, set.printedTotal);
            ins.step();
        }

        Statement meta(db_,
                       "INSERT INTO cache_meta(key, value) VALUES(?, ?)"
                       " ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
        meta.bindText(1, kFetchedAtKey);
        meta.bindText(2, timestampToIso(fetchedAt));
        meta.step();
    } catch (...) {
        try {
            db_.exec("ROLLBACK;");
        } catch (...) {
            // The transaction is already doomed; surface the original failure.
        }
        throw;
    }
    db_.exec("COMMIT;");
}

}  // namespace pokedex
