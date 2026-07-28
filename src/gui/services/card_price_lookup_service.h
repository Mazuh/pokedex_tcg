#pragma once

#include <QObject>
#include <QSet>
#include <QString>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/app/card_price_dto.h"
#include "core/domain/card_reference.h"
#include "core/domain/types.h"

class QNetworkAccessManager;

namespace pokedex {

class CardPriceService;
class CardSetCache;

// GUI — the transport half of the card-price module: fetches ONE card's market
// prices on demand and persists them via the Qt-free CardPriceService. The sibling
// of CardSearchService, but deliberately much simpler: there is no browse/stream,
// just "fetch this card's prices" driven by an explicit user action (a Fetch/Refresh
// button) — the app never fetches a price just because a card was shown (the
// on-demand rule, so we never hammer a free API for cards the user may not even own).
//
// The pricing PROVIDER is tcgdex (https://api.tcgdex.net) — a free aggregator, not an
// authoritative source of truth. It is used because, unlike the pokemontcg.io metadata
// catalog, it covers brand-new sets and is addressable by set+collector-number, so a card
// the metadata catalog hasn't ingested can still be priced. A card is addressed by its
// tcgdex id ("mep-013" == setId "-" localId); an owned copy that isn't yet linked to one is
// resolved to it directly from its printed set+number via the tcgdex set table (no catalog
// SEARCH), which this service lazily fetches and holds for the session (resolveTcgdexId /
// ensureTcgdexSets).
//
// Reads are free: cachedPrices()/cachedMany() hit only the local cache, no network.
// fetch() (and the one-time set-table fetch behind ensureTcgdexSets) are the only methods
// that may touch the wire.
class CardPriceLookupService : public QObject {
    Q_OBJECT

public:
    // `prices` must outlive this service (like the other GUI transports). `setCache`, when
    // supplied, is a tcgdex-scoped CardSetCache the set table is loaded from / persisted to,
    // so the /v2/en/sets fetch is skipped on most launches (a 24h TTL) and a stale copy still
    // resolves ids when the API is down — the same disk-cache treatment the catalog set table
    // gets. Null disables persistence (the table is then fetched fresh each session).
    explicit CardPriceLookupService(CardPriceService& prices, CardSetCache* setCache = nullptr,
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

    // Fetch this card's prices from the tcgdex per-card endpoint, persist them (replacing
    // the API-sourced rows, keeping manual ones), and emit pricesReady(id). A blank id is
    // ignored (an unresolved copy). Always driven by an explicit user action (a
    // Fetch/Refresh button), so it always hits the wire — subject only to coalescing a
    // fetch already in flight for the same id. On terminal failure emits pricesFailed(id).
    void fetch(const QString& externalCardId);

    // Whether the tcgdex set table is loaded, so resolveTcgdexId can map a copy's set to a
    // tcgdex set id. False until the first successful ensureTcgdexSets().
    bool tcgdexSetsReady() const { return tcgdexSetsLoaded_; }

    // Resolve a copy's printed identity to a tcgdex card id for the price lookup, using the
    // in-memory set table. Returns nullopt when the table isn't loaded yet or the set/number
    // can't be identified (see core resolveTcgdexCardId). No network — call ensureTcgdexSets
    // first if tcgdexSetsReady() is false.
    std::optional<QString> resolveTcgdexId(const CardReference& ref) const;

    // Ensure the tcgdex set table is loaded, then emit tcgdexSetsResolved(ok). If it is
    // already loaded (or a fresh disk copy exists) this emits immediately (ok=true) without a
    // network hit; otherwise it fetches /v2/en/sets once (coalescing concurrent callers onto
    // the one in-flight fetch). `forceRefresh` bypasses both the in-memory table and the
    // fresh-disk shortcut to fetch a current list — the caller uses it after a resolve MISS,
    // since a set released since the cached copy would otherwise stay unknown until the TTL.
    void ensureTcgdexSets(bool forceRefresh = false);

Q_SIGNALS:
    // Fired when a card's prices are available to (re-)read via cachedPrices(id) — after a
    // successful fetch.
    void pricesReady(const QString& externalCardId);
    void pricesFailed(const QString& externalCardId);
    // Fired when ensureTcgdexSets() finishes: ok=true when the set table is loaded (a
    // subsequent resolveTcgdexId can succeed), false when the fetch failed (offer a retry).
    void tcgdexSetsResolved(bool ok);

private:
    void startFetch(const QString& externalCardId, int retriesLeft);
    void startSetsFetch(int retriesLeft);
    // Load the tcgdex set table from the disk cache into memory. `requireFresh` demands the
    // cache be within the TTL (the no-network fast path); false accepts any age (the
    // post-failure stale fallback). Returns true when a non-empty table was adopted.
    bool loadSetsFromCache(bool requireFresh);
    // Terminal outcomes of an in-flight price fetch. On success the coalesced-Refresh flag is
    // cleared (the fresh result covers every waiting panel); on failure, if a Refresh
    // coalesced onto this now-failed attempt, it is re-issued as a genuine fresh fetch
    // rather than inheriting the failure.
    void finishSucceeded(const QString& externalCardId);
    void finishFailed(const QString& externalCardId);

    CardPriceService& prices_;
    CardSetCache* setCache_;  // tcgdex-scoped disk cache for the set table (may be null)
    QNetworkAccessManager* nam_;
    QSet<QString> inFlight_;  // ids with a fetch on the wire, to coalesce duplicates
    // ids for which an explicit Fetch/Refresh arrived while a fetch was already on the
    // wire. A coalesced request rides the in-flight result when it SUCCEEDS, but if that
    // fetch fails the user's forced click still deserves a real attempt — so it is
    // re-issued once, on failure, for any id recorded here.
    QSet<QString> refetchQueued_;

    // The tcgdex set table (id + name), fetched once per session and held in memory to map a
    // copy's printed set → a tcgdex set id. Small, near-static reference data.
    std::vector<CardSetInfo> tcgdexSets_;
    bool tcgdexSetsLoaded_ = false;
    bool tcgdexSetsFetching_ = false;  // a /v2/en/sets fetch is on the wire (coalesce callers)
};

}  // namespace pokedex
