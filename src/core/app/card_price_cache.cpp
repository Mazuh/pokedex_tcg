#include "core/app/card_price_cache.h"

#include <algorithm>

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {

// The nine SELECTed columns of one card_price row, in the order every read query below
// projects them — one place so the single-card and batch reads can't drift.
constexpr const char* kPriceColumns =
    "id, external_card_id, provenance, variant, metric, amount_cents, currency, observed_at,"
    " note";

// Map the current row of a stepped SELECT (projecting kPriceColumns) into a CardPrice.
CardPrice readPriceRow(Statement& stmt) {
    CardPrice p;
    p.id = stmt.columnText(0);
    p.externalCardId = stmt.columnText(1);
    p.provenance = stmt.columnText(2);
    p.variant = stmt.columnText(3);
    p.metric = stmt.columnText(4);
    p.amountCents = stmt.columnInt64(5);
    p.currency = stmt.columnText(6);
    p.observedAt = timestampFromIso(stmt.columnText(7));
    p.note = stmt.columnText(8);
    return p;
}

// Run a chunked `<sqlPrefix> ?,?,… <sqlSuffix>` SELECT over `ids`, each chunk staying under
// SQLite's bound-parameter limit, invoking `onRow(stmt)` for every result row. `sqlPrefix` ends
// at the open paren of the `IN (` clause and `sqlSuffix` begins at its close, so the caller
// supplies the columns/table/order and this owns only the placeholder list + per-chunk binding —
// the batch-read shape both by-many reads share. Rows for one id always land in a single chunk
// (ids are distinct), so a per-chunk ORDER BY still groups each id's rows correctly.
template <typename RowFn>
void selectByIdChunks(Database& db, const std::string& sqlPrefix, const std::string& sqlSuffix,
                      const std::vector<std::string>& ids, RowFn onRow) {
    constexpr std::size_t kChunk = 500;
    for (std::size_t start = 0; start < ids.size(); start += kChunk) {
        const std::size_t count = std::min(kChunk, ids.size() - start);
        std::string sql = sqlPrefix;
        for (std::size_t i = 0; i < count; ++i) {
            sql += (i == 0) ? "?" : ",?";
        }
        sql += sqlSuffix;
        Statement stmt(db, sql);
        for (std::size_t i = 0; i < count; ++i) {
            stmt.bindText(static_cast<int>(i + 1), ids[start + i]);
        }
        while (stmt.step()) {
            onRow(stmt);
        }
    }
}

// Insert one already-id'd price row into card_price.
void insertPrice(Database& db, const CardPrice& p) {
    Statement ins(db,
                  "INSERT INTO card_price(id, external_card_id, provenance, variant, metric,"
                  " amount_cents, currency, observed_at, note)"
                  " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);");
    ins.bindText(1, p.id);
    ins.bindText(2, p.externalCardId);
    ins.bindText(3, p.provenance);
    ins.bindText(4, p.variant);
    ins.bindText(5, p.metric);
    ins.bindInt64(6, p.amountCents);
    ins.bindText(7, p.currency);
    ins.bindText(8, timestampToIso(p.observedAt));
    ins.bindText(9, p.note);
    ins.step();
}

}  // namespace

std::vector<CardPrice> CardPriceCache::pricesFor(const std::string& externalCardId) {
    Statement stmt(db_, std::string("SELECT ") + kPriceColumns +
                            " FROM card_price WHERE external_card_id = ?"
                            " ORDER BY provenance, variant, metric;");
    stmt.bindText(1, externalCardId);
    std::vector<CardPrice> prices;
    while (stmt.step()) {
        prices.push_back(readPriceRow(stmt));
    }
    return prices;
}

std::unordered_map<std::string, std::vector<CardPrice>> CardPriceCache::pricesForMany(
    const std::vector<std::string>& externalCardIds) {
    // One query for the whole set (chunked to stay under SQLite's bound-parameter limit)
    // instead of one SELECT per id — so a caller totalling a binder's value reads every
    // card's prices in a couple of round-trips, not N. Rows are grouped by card id; the
    // per-card order matches pricesFor (provenance, variant, metric).
    std::unordered_map<std::string, std::vector<CardPrice>> byId;
    selectByIdChunks(
        db_, std::string("SELECT ") + kPriceColumns + " FROM card_price WHERE external_card_id IN (",
        ") ORDER BY external_card_id, provenance, variant, metric;", externalCardIds,
        [&](Statement& stmt) {
            CardPrice p = readPriceRow(stmt);
            byId[p.externalCardId].push_back(std::move(p));
        });
    return byId;
}

std::optional<Timestamp> CardPriceCache::fetchedAt(const std::string& externalCardId) {
    Statement stmt(db_, "SELECT fetched_at FROM card_price_fetch WHERE external_card_id = ?;");
    stmt.bindText(1, externalCardId);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return timestampFromIso(stmt.columnText(0));
}

void CardPriceCache::storeApiPrices(const std::string& externalCardId,
                                    const std::vector<CardPrice>& prices,
                                    Timestamp fetchedAt) {
    // The row replacement and its fetch stamp are one logical unit across several
    // statements, so they run in a transaction. A mid-write failure must not leave
    // rows that disagree with the recorded fetch time.
    db_.transaction([&] {
        // Drop only this card's API-sourced rows; manual rows survive a refetch.
        Statement clear(db_,
                        "DELETE FROM card_price WHERE external_card_id = ? AND provenance != ?;");
        clear.bindText(1, externalCardId);
        clear.bindText(2, kManualPriceProvenance);
        clear.step();

        for (const CardPrice& p : prices) {
            insertPrice(db_, p);
        }

        Statement meta(db_,
                       "INSERT INTO card_price_fetch(external_card_id, fetched_at) VALUES(?, ?)"
                       " ON CONFLICT(external_card_id) DO UPDATE SET fetched_at = excluded.fetched_at;");
        meta.bindText(1, externalCardId);
        meta.bindText(2, timestampToIso(fetchedAt));
        meta.step();
    });
}

void CardPriceCache::clear(const std::string& externalCardId) {
    // Forget everything for this card — every price row (API-sourced AND manual), the fetch
    // stamp, AND the vendor suppressions — so it returns to the never-fetched, nothing-hidden
    // "ground zero" state. One transaction so a mid-delete failure can't leave a partial state.
    db_.transaction([&] {
        Statement rows(db_, "DELETE FROM card_price WHERE external_card_id = ?;");
        rows.bindText(1, externalCardId);
        rows.step();

        Statement stamp(db_, "DELETE FROM card_price_fetch WHERE external_card_id = ?;");
        stamp.bindText(1, externalCardId);
        stamp.step();

        Statement supp(db_, "DELETE FROM card_price_suppression WHERE external_card_id = ?;");
        supp.bindText(1, externalCardId);
        supp.step();
    });
}

void CardPriceCache::suppressVendor(const std::string& externalCardId,
                                    const std::string& provenance) {
    Statement stmt(db_,
                   "INSERT INTO card_price_suppression(external_card_id, provenance)"
                   " VALUES(?, ?) ON CONFLICT(external_card_id, provenance) DO NOTHING;");
    stmt.bindText(1, externalCardId);
    stmt.bindText(2, provenance);
    stmt.step();
}

void CardPriceCache::unsuppressVendor(const std::string& externalCardId,
                                      const std::string& provenance) {
    Statement stmt(db_,
                   "DELETE FROM card_price_suppression"
                   " WHERE external_card_id = ? AND provenance = ?;");
    stmt.bindText(1, externalCardId);
    stmt.bindText(2, provenance);
    stmt.step();
}

std::vector<std::string> CardPriceCache::suppressedVendors(const std::string& externalCardId) {
    Statement stmt(db_,
                   "SELECT provenance FROM card_price_suppression WHERE external_card_id = ?"
                   " ORDER BY provenance;");
    stmt.bindText(1, externalCardId);
    std::vector<std::string> vendors;
    while (stmt.step()) {
        vendors.push_back(stmt.columnText(0));
    }
    return vendors;
}

std::unordered_map<std::string, std::vector<std::string>> CardPriceCache::suppressedVendorsForMany(
    const std::vector<std::string>& externalCardIds) {
    // One query per chunk (under SQLite's bound-parameter limit) instead of one per id, so a
    // card table reads every card's suppressions in a couple of round-trips. Cards with none
    // are simply absent from the map (the reader treats "absent" as "nothing hidden").
    std::unordered_map<std::string, std::vector<std::string>> byId;
    selectByIdChunks(
        db_,
        "SELECT external_card_id, provenance FROM card_price_suppression WHERE external_card_id IN (",
        ") ORDER BY external_card_id, provenance;", externalCardIds,
        [&](Statement& stmt) { byId[stmt.columnText(0)].push_back(stmt.columnText(1)); });
    return byId;
}

void CardPriceCache::add(const CardPrice& price) { insertPrice(db_, price); }

void CardPriceCache::removeManual(const std::string& id) {
    Statement stmt(db_, "DELETE FROM card_price WHERE id = ? AND provenance = ?;");
    stmt.bindText(1, id);
    stmt.bindText(2, kManualPriceProvenance);
    stmt.step();
}

}  // namespace pokedex
