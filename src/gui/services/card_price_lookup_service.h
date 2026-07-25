#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <optional>
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
// Reads are free: cached()/fetchedAt()/needsRefresh() hit only the local cache, no
// network. fetch() is the only method that may touch the wire.
class CardPriceLookupService : public QObject {
    Q_OBJECT

public:
    // `api` and `prices` must outlive this service (like the other GUI transports).
    // `api` is reused from the search service's composition — the same pokemontcg.io
    // adapter resolves both search and per-card URLs.
    explicit CardPriceLookupService(const CardCatalogApi& api, CardPriceService& prices,
                                    QObject* parent = nullptr);

    // The prices already cached for a card (empty if none) — no network.
    std::vector<CardPrice> cached(const QString& externalCardId);

    // When we last fetched this card from the API, or nullopt if never — no network.
    std::optional<Timestamp> fetchedAt(const QString& externalCardId);

    // Whether a fresh fetch is warranted (never fetched, or older than the TTL) — no
    // network. A view uses it only as a hint (e.g. to label a Refresh button); it
    // never triggers a fetch on its own.
    bool needsRefresh(const QString& externalCardId);

    // Fetch this card's prices from the per-card endpoint, persist them (replacing the
    // API-sourced rows, keeping manual ones), and emit pricesReady(id). A blank id is
    // ignored (an unlinked copy). When `force` is false and the cache is still fresh,
    // emits pricesReady immediately from cache with NO network call; `force` (an
    // explicit Refresh) always hits the wire. A fetch already in flight for the same
    // id is coalesced. On terminal failure emits pricesFailed(id).
    void fetch(const QString& externalCardId, bool force = false);

Q_SIGNALS:
    // Fired when a card's prices are available to (re-)read via cached(id) — after a
    // successful fetch, or immediately when a non-forced fetch found a fresh cache.
    void pricesReady(const QString& externalCardId);
    void pricesFailed(const QString& externalCardId);

private:
    void startFetch(const QString& externalCardId, int retriesLeft);

    const CardCatalogApi& api_;
    CardPriceService& prices_;
    QNetworkAccessManager* nam_;
    QSet<QString> inFlight_;  // ids with a fetch on the wire, to coalesce duplicates
};

}  // namespace pokedex
