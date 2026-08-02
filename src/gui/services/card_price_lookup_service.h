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

    // Forget this card's cached prices (fetched + manual), its fetch stamp, AND its vendor
    // suppressions, then emit pricesReady(id) so every view showing the card re-reads the (now
    // empty) cache and returns to the not-fetched, nothing-hidden state. No network. A blank id
    // is ignored.
    void clearPrices(const QString& externalCardId);

    // Hide / un-hide a vendor for a card (a per-card, per-vendor suppression), then emit
    // pricesReady(id) so every view showing the card re-renders with the vendor gone / back. No
    // network. A blank id is ignored. A suppression persists across a Refresh; only clearPrices
    // drops it — so a vendor whose tcgdex mapping is wrong for the copy stays hidden until Clear.
    void suppressVendor(const QString& externalCardId, const QString& provenance);
    void unsuppressVendor(const QString& externalCardId, const QString& provenance);

    // A card's suppressed vendors, and the same batched for many cards (for the tables). No
    // network. Cards with no suppression are absent from the map.
    std::vector<std::string> suppressedVendors(const QString& externalCardId);
    std::unordered_map<std::string, std::vector<std::string>> suppressedVendorsMany(
        const std::vector<std::string>& externalCardIds);

    // Whether this card's cached prices are still within the auto-fetch freshness window (24h),
    // so an AUTOMATIC fetch (kicked off when a copy is added) can skip the network and read the
    // cache instead of re-hitting a free API for a card just priced. A manual Fetch/Refresh
    // ignores this and always hits the wire. False when never fetched or the id is blank. No
    // network. (Reads the fetch stamp, so not const.)
    bool pricesFresh(const QString& externalCardId);

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

    // Bulk re-fetch: fetch() every one of these (assumed distinct) already-linked ids, paced so
    // at most kBulkMaxConcurrent are on the wire at once — a binder's / My Cards' "Refresh all
    // prices". Because there is ONE service, the cap and the "one bulk at a time" guard are
    // global across every view (a second refreshMany while one runs is a no-op). Advances only on
    // real fetch completions (finishSucceeded/finishFailed), never on a suppress/clear that also
    // emits pricesReady, so the cap and progress stay correct. Emits bulkProgress as each lands
    // and bulkFinished when all are done. A no-op for an empty list.
    void refreshMany(const std::vector<std::string>& externalCardIds);
    bool bulkRunning() const { return bulkTotal_ > 0; }

Q_SIGNALS:
    // Fired when a card's prices are available to (re-)read via cachedPrices(id) — after a
    // successful fetch.
    void pricesReady(const QString& externalCardId);
    void pricesFailed(const QString& externalCardId);
    // Fired when ensureTcgdexSets() finishes: ok=true when the set table is loaded (a
    // subsequent resolveTcgdexId can succeed), false when the fetch failed (offer a retry).
    void tcgdexSetsResolved(bool ok);
    // A background auto-fetch (kicked off when a copy was added) resolved and persisted that
    // copy's tcgdex link. A host that reloaded BEFORE the (async, cold-set-table) link landed
    // connects this to write the id into its in-memory copy vector, so the auto-fetch's
    // subsequent pricesReady isn't dropped by the host's "do I hold a copy with this id?" guard
    // and the Prices column fills in. Relayed from the fire-and-forget controller's cardLinked.
    void copyAutoLinked(const QString& copyId, const QString& externalCardId);
    // A bulk refresh (refreshMany) progressed / completed. `done`/`total` count settled cards.
    void bulkProgress(int done, int total);
    void bulkFinished();

private:
    // Start a price fetch for `externalCardId` (the canonical id — the copy's link and cache
    // key). startFetch picks the initial request URL (applying any learned set padding, below),
    // then startFetchUrl does the GET: `urlId` is what goes in the URL and may differ from
    // externalCardId when a zero-padded collector number is needed to address the card on tcgdex
    // (modern sets pad, older ones don't — see paddedCardIdCandidate). On a 404 it retries the
    // other id form once (printed ↔ zero-padded); `triedAlternate` is that one-shot guard, set on
    // the fallback attempt so it can't ping-pong. The prices are always recorded under
    // externalCardId regardless of which URL variant served them, so nothing downstream (the
    // link, the cache key, the signals) sees the padded form.
    void startFetch(const QString& externalCardId, int retriesLeft);
    void startFetchUrl(const QString& externalCardId, const QString& urlId, int retriesLeft,
                       bool triedAlternate);
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
    // Start bulk fetches up to the concurrency cap; advance the bulk when one of its ids settles
    // (called from finishSucceeded/finishFailed only, so a suppress/clear never miscounts it).
    void pumpBulk();
    void advanceBulk(const QString& externalCardId);

    CardPriceService& prices_;
    CardSetCache* setCache_;  // tcgdex-scoped disk cache for the set table (may be null)
    QNetworkAccessManager* nam_;
    QSet<QString> inFlight_;  // ids with a fetch on the wire, to coalesce duplicates
    // ids for which an explicit Fetch/Refresh arrived while a fetch was already on the
    // wire. A coalesced request rides the in-flight result when it SUCCEEDS, but if that
    // fetch fails the user's forced click still deserves a real attempt — so it is
    // re-issued once, on failure, for any id recorded here.
    QSet<QString> refetchQueued_;

    // tcgdex set-id prefixes ("me02") we've learned zero-pad their collector numbers, discovered
    // when a padded URL succeeded after the as-printed one 404'd. Lets the other cards of that set
    // (notably the rest of a bulk refresh) go straight to the padded URL instead of each spending
    // a 404 to rediscover it. Session-only; a wrong guess only ever costs one extra fetch, so it
    // needn't persist.
    QSet<QString> paddingSets_;

    // The tcgdex set table (id + name), fetched once per session and held in memory to map a
    // copy's printed set → a tcgdex set id. Small, near-static reference data.
    std::vector<CardSetInfo> tcgdexSets_;
    bool tcgdexSetsLoaded_ = false;
    bool tcgdexSetsFetching_ = false;  // a /v2/en/sets fetch is on the wire (coalesce callers)

    // Bulk refresh (refreshMany) state — a single queue shared across every view, so the cap is
    // global. bulkTotal_ > 0 == a bulk is running (bulkRunning()).
    std::vector<std::string> bulkPending_;   // ids not yet started
    QSet<QString> bulkInFlight_;             // ids fetched, awaiting finishSucceeded/finishFailed
    int bulkTotal_ = 0;
    int bulkDone_ = 0;
    static constexpr int kBulkMaxConcurrent = 4;  // anti-burst cap on the free tcgdex API
};

}  // namespace pokedex
