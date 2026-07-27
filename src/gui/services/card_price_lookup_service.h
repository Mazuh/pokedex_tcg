#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/types.h"

class QNetworkAccessManager;

namespace pokedex {

class CardCatalogApi;
class CardPriceService;

// GUI — the transport half of the card-price module: fetches ONE card's market
// prices from the external per-card endpoint on demand and persists them via the
// Qt-free CardPriceService. The sibling of CardSearchService, but deliberately much
// simpler: there is no browse/stream, just "fetch this card's prices" driven by an
// explicit user action (a Fetch/Refresh button) — the app never fetches a price
// just because a card was shown (the on-demand rule, so we never hammer the free API
// for cards the user may not even own).
//
// Reads are free: cachedPrices()/cachedMany() hit only the local cache, no network.
// fetch() is the only method that may touch the wire.
class CardPriceLookupService : public QObject {
    Q_OBJECT

public:
    // `api` and `prices` must outlive this service (like the other GUI transports).
    // `api` is reused from the search service's composition — the same pokemontcg.io
    // adapter resolves both search and per-card URLs.
    explicit CardPriceLookupService(const CardCatalogApi& api, CardPriceService& prices,
                                    QObject* parent = nullptr);

    // A card's cached prices together with its last-fetch stamp, so a view rendering a
    // selection consults the cache through one call and one error path instead of two
    // (`cached` + `fetchedAt`). Prices and the stamp live in separate tables, so this is
    // a single accessor over two small local reads — not a single query — but it keeps a
    // per-selection read in one place. No network.
    struct CachedPrices {
        std::vector<CardPrice> prices;
        std::optional<Timestamp> fetchedAt;  // nullopt if never fetched
    };
    CachedPrices cachedPrices(const QString& externalCardId);

    // The cached prices for many cards at once, keyed by external card id — one batched
    // query instead of N cachedPrices() calls, for a caller totalling a collection's
    // value (e.g. a binder header). No network.
    std::unordered_map<std::string, std::vector<CardPrice>> cachedMany(
        const std::vector<std::string>& externalCardIds);

    // Fetch this card's prices from the per-card endpoint, persist them (replacing the
    // API-sourced rows, keeping manual ones), and emit pricesReady(id). A blank id is
    // ignored (an unlinked copy). Always driven by an explicit user action (a
    // Fetch/Refresh button), so it always hits the wire — subject only to coalescing a
    // fetch already in flight for the same id. On terminal failure emits pricesFailed(id).
    void fetch(const QString& externalCardId);

Q_SIGNALS:
    // Fired when a card's prices are available to (re-)read via cached(id) — after a
    // successful fetch, or immediately when a non-forced fetch found a fresh cache.
    void pricesReady(const QString& externalCardId);
    void pricesFailed(const QString& externalCardId);

private:
    void startFetch(const QString& externalCardId, int retriesLeft);
    // Terminal outcomes of an in-flight fetch. On success the coalesced-Refresh flag is
    // cleared (the fresh result covers every waiting panel); on failure, if a Refresh
    // coalesced onto this now-failed attempt, it is re-issued as a genuine fresh fetch
    // rather than inheriting the failure.
    void finishSucceeded(const QString& externalCardId);
    void finishFailed(const QString& externalCardId);

    const CardCatalogApi& api_;
    CardPriceService& prices_;
    QNetworkAccessManager* nam_;
    QSet<QString> inFlight_;  // ids with a fetch on the wire, to coalesce duplicates
    // ids for which an explicit Fetch/Refresh arrived while a fetch was already on the
    // wire. A coalesced request rides the in-flight result when it SUCCEEDS, but if that
    // fetch fails the user's forced click still deserves a real attempt — so it is
    // re-issued once, on failure, for any id recorded here.
    QSet<QString> refetchQueued_;
};

}  // namespace pokedex
