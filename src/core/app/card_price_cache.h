#pragma once

#include <optional>
#include <string>
#include <unordered_map>
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
    std::vector<CardPrice> pricesFor(const std::string& externalCardId);

    // The stored prices for many cards at once, keyed by external card id (a card with
    // no rows is simply absent from the map) — one batched query instead of N calls to
    // pricesFor, for callers that total or display a whole collection's prices. Each
    // card's rows keep pricesFor's (provenance, variant, metric) order.
    std::unordered_map<std::string, std::vector<CardPrice>> pricesForMany(
        const std::vector<std::string>& externalCardIds);

    // When WE last fetched this card's prices from the API, or nullopt if never —
    // the caller compares it against "now" to decide whether to refetch (TTL).
    std::optional<Timestamp> fetchedAt(const std::string& externalCardId);

    // Replace this card's API-sourced prices with `prices` and stamp `fetchedAt`, in
    // one transaction. Manual rows (provenance == kManualPriceProvenance) are left
    // untouched — a refetch never discards a hand-entered price. An empty `prices`
    // still records the fetch stamp (the card simply has no market price right now).
    // Each row's `id` must already be set (minted by the caller).
    void storeApiPrices(const std::string& externalCardId, const std::vector<CardPrice>& prices,
                        Timestamp fetchedAt);

    // Forget everything cached for this card — every price row (API-sourced AND manual) and
    // the fetch stamp — returning it to the never-fetched state (a later fetch re-populates
    // it). One transaction. A no-op when nothing is cached for the id.
    void clear(const std::string& externalCardId);

    // Insert one price row (used for a manual entry). Its `id` must already be set.
    void add(const CardPrice& price);

    // Delete a manual price row by id. A no-op if the id does not exist or is not a
    // manual row — API-sourced rows are managed only through storeApiPrices.
    void removeManual(const std::string& id);

    // Hide a vendor's price for a card (a per-card, per-vendor suppression), and un-hide it.
    // These live in their own table (card_price_suppression), so a Refresh — which rewrites
    // card_price — never disturbs them; only clear() drops them. Idempotent.
    void suppressVendor(const std::string& externalCardId, const std::string& provenance);
    void unsuppressVendor(const std::string& externalCardId, const std::string& provenance);

    // The vendors currently suppressed for a card, and the same batched for many cards (for the
    // card tables) — cards with no suppression are simply absent from the map.
    std::vector<std::string> suppressedVendors(const std::string& externalCardId);
    std::unordered_map<std::string, std::vector<std::string>> suppressedVendorsForMany(
        const std::vector<std::string>& externalCardIds);

private:
    Database& db_;
};

}  // namespace pokedex
