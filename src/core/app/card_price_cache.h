#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/types.h"

namespace pokedex {

class Database;

// APP — the local persistence of a card's market prices (the card_price /
// card_price_fetch tables, schema v7). Like CardSetCache it lives in app/, not
// storage/, because its row type CardPrice is an app-layer projection of external
// data; a storage-layer repository referencing it would invert the layering.
//
// It is a thin SQL accessor: ids and the clock come from CardPriceService (the app
// layer mints ids, never the storage/cache layer). Prices are keyed by the external
// card id, independent of any owned CardCopy — this cache exists so the on-demand
// per-card price fetch is not repeated against the free API more than a TTL allows,
// while still letting the user pin manual prices.
class CardPriceCache {
public:
    // `db` must outlive this accessor (like every repository).
    explicit CardPriceCache(Database& db) : db_(db) {}

    // Every stored price for a card — API-sourced and manual together — ordered
    // deterministically by (provenance, variant, metric). Empty when none stored.
    std::vector<CardPrice> pricesFor(const std::string& cardKey);

    // When WE last fetched this card's prices from the API, or nullopt if never —
    // the caller compares it against "now" to decide whether to refetch (TTL).
    std::optional<Timestamp> fetchedAt(const std::string& cardKey);

    // Replace this card's API-sourced prices with `prices` and stamp `fetchedAt`, in
    // one transaction. Manual rows (provenance == kManualPriceProvenance) are left
    // untouched — a refetch never discards a hand-entered price. An empty `prices`
    // still records the fetch stamp (the card simply has no market price right now).
    // Each row's `id` must already be set (minted by the caller).
    void storeApiPrices(const std::string& cardKey, const std::vector<CardPrice>& prices,
                        Timestamp fetchedAt);

    // Insert one price row (used for a manual entry). Its `id` must already be set.
    void add(const CardPrice& price);

    // Delete a manual price row by id. A no-op if the id does not exist or is not a
    // manual row — API-sourced rows are managed only through storeApiPrices.
    void removeManual(const std::string& id);

private:
    Database& db_;
};

}  // namespace pokedex
