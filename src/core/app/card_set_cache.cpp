#include "core/app/card_set_cache.h"

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {
// The cache_meta key holding this source's last-fetch stamp, e.g. "sets_fetched_at:tcgdex".
// Per-source so each provider's table has its own TTL.
std::string fetchedAtKey(const std::string& source) { return "sets_fetched_at:" + source; }
}  // namespace

std::vector<CardSetInfo> CardSetCache::load() {
    Statement stmt(db_,
                   "SELECT id, ptcgo_code, name, printed_total"
                   " FROM card_set_cache WHERE source = ? ORDER BY id;");
    stmt.bindText(1, source_);
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
    stmt.bindText(1, fetchedAtKey(source_));
    if (!stmt.step()) {
        return std::nullopt;
    }
    return timestampFromIso(stmt.columnText(0));
}

void CardSetCache::store(const std::vector<CardSetInfo>& sets, Timestamp fetchedAt) {
    // The row replacement and its timestamp are one logical unit spread across
    // several statements, so they run in a transaction. Otherwise a mid-write failure
    // could leave rows that disagree with the recorded fetch time. Only THIS source's
    // rows are cleared/rewritten, so another provider's cache is untouched.
    db_.transaction([&] {
        Statement clear(db_, "DELETE FROM card_set_cache WHERE source = ?;");
        clear.bindText(1, source_);
        clear.step();

        for (const CardSetInfo& set : sets) {
            Statement ins(db_,
                          "INSERT INTO card_set_cache(source, id, ptcgo_code, name, printed_total)"
                          " VALUES(?, ?, ?, ?, ?);");
            ins.bindText(1, source_);
            ins.bindText(2, set.id);
            ins.bindText(3, set.ptcgoCode);
            ins.bindText(4, set.name);
            ins.bindInt(5, set.printedTotal);
            ins.step();
        }

        Statement meta(db_,
                       "INSERT INTO cache_meta(key, value) VALUES(?, ?)"
                       " ON CONFLICT(key) DO UPDATE SET value = excluded.value;");
        meta.bindText(1, fetchedAtKey(source_));
        meta.bindText(2, timestampToIso(fetchedAt));
        meta.step();
    });
}

}  // namespace pokedex
