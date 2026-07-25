#include "core/app/card_price_cache.h"

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

namespace {

// Insert one already-id'd price row into card_price.
void insertPrice(Database& db, const CardPrice& p) {
    Statement ins(db,
                  "INSERT INTO card_price(id, card_key, provenance, variant, metric,"
                  " amount_cents, currency, observed_at, note)"
                  " VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?);");
    ins.bindText(1, p.id);
    ins.bindText(2, p.cardKey);
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

std::vector<CardPrice> CardPriceCache::pricesFor(const std::string& cardKey) {
    Statement stmt(db_,
                   "SELECT id, card_key, provenance, variant, metric, amount_cents,"
                   " currency, observed_at, note FROM card_price WHERE card_key = ?"
                   " ORDER BY provenance, variant, metric;");
    stmt.bindText(1, cardKey);
    std::vector<CardPrice> prices;
    while (stmt.step()) {
        CardPrice p;
        p.id = stmt.columnText(0);
        p.cardKey = stmt.columnText(1);
        p.provenance = stmt.columnText(2);
        p.variant = stmt.columnText(3);
        p.metric = stmt.columnText(4);
        p.amountCents = stmt.columnInt64(5);
        p.currency = stmt.columnText(6);
        p.observedAt = timestampFromIso(stmt.columnText(7));
        p.note = stmt.columnText(8);
        prices.push_back(std::move(p));
    }
    return prices;
}

std::optional<Timestamp> CardPriceCache::fetchedAt(const std::string& cardKey) {
    Statement stmt(db_, "SELECT fetched_at FROM card_price_fetch WHERE card_key = ?;");
    stmt.bindText(1, cardKey);
    if (!stmt.step()) {
        return std::nullopt;
    }
    return timestampFromIso(stmt.columnText(0));
}

void CardPriceCache::storeApiPrices(const std::string& cardKey,
                                    const std::vector<CardPrice>& prices,
                                    Timestamp fetchedAt) {
    // The row replacement and its fetch stamp are one logical unit across several
    // statements, so they run in a transaction with a best-effort ROLLBACK —
    // mirroring CardSetCache::store and migrate(). A mid-write failure must not
    // leave rows that disagree with the recorded fetch time.
    db_.exec("BEGIN;");
    try {
        // Drop only this card's API-sourced rows; manual rows survive a refetch.
        Statement clear(db_,
                        "DELETE FROM card_price WHERE card_key = ? AND provenance != ?;");
        clear.bindText(1, cardKey);
        clear.bindText(2, kManualPriceProvenance);
        clear.step();

        for (const CardPrice& p : prices) {
            insertPrice(db_, p);
        }

        Statement meta(db_,
                       "INSERT INTO card_price_fetch(card_key, fetched_at) VALUES(?, ?)"
                       " ON CONFLICT(card_key) DO UPDATE SET fetched_at = excluded.fetched_at;");
        meta.bindText(1, cardKey);
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

void CardPriceCache::add(const CardPrice& price) { insertPrice(db_, price); }

void CardPriceCache::removeManual(const std::string& id) {
    Statement stmt(db_, "DELETE FROM card_price WHERE id = ? AND provenance = ?;");
    stmt.bindText(1, id);
    stmt.bindText(2, kManualPriceProvenance);
    stmt.step();
}

}  // namespace pokedex
