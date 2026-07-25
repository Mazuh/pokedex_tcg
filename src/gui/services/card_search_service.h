#pragma once

#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/app/rate_limiter.h"

class QNetworkAccessManager;
class QTimer;
class QPixmap;

namespace pokedex {

class CardCatalogApi;
class CardSetCache;

// GUI — the transport half of the card-catalog module: a QObject that searches a
// species' printings on the external card API and streams the results (and their
// thumbnails) to the AddCardCopyPage. It depends only on the Qt-free CardCatalogApi
// seam for *where* to GET and on the core parsers for turning the JSON into plain
// CardCandidate structs; it never names pokemontcg.io itself.
//
// Deliberately DIFFERENT from MediaService in one key way: it NEVER caches to disk.
// A user scanning a species' dozens of printings would otherwise pull dozens of
// large images for nothing — search results are display/memory-only. (Persisting
// the one image a committed copy keeps is a separate, future concern.)
//
// The set table (/v2/sets) backs both the expansion-code picker and the set.id-based
// search narrowing (the printed ptcgoCode search index is unreliable). It is loaded
// once and kept in memory for the app's lifetime. When a CardSetCache is supplied,
// that load prefers the on-disk cache: a cache younger than kSetCacheTtl is used as
// is (NO network fetch — the daily-flaky /v2/sets is skipped on most launches), and
// a fresh network fetch overwrites the cache. If the fetch fails outright, a stale
// cache is loaded as a fallback so search narrowing still works while the API is
// down. Without a cache (e.g. a bare test construction) it always fetches, as before.
//
// The network path is throttled like MediaService — a latest-wins debounce (so
// typing an expansion-code filter doesn't fire a request per keystroke) plus a
// token-bucket backstop shared by searches and thumbnail fetches. Card searches
// additionally retry with backoff on transient failures (the API 504s under load).
// Results are stale-guarded by a monotonic generation counter: only the most recent
// request's reply is emitted, so a superseded search (new species or new filter) is
// silently dropped.
class CardSearchService : public QObject {
    Q_OBJECT

public:
    // `api` must outlive this service. `cache`, when non-null, persists the set
    // table across launches (see the class note); it too must outlive this service.
    explicit CardSearchService(const CardCatalogApi& api, CardSetCache* cache = nullptr,
                               QObject* parent = nullptr);

    // Search `dexNumber`'s printings, optionally narrowed to the set whose printed
    // code is `setCodeFilter` (blank = every printing). Debounced; returns a unique
    // request id that the eventual printingsReady()/printingsFailed() carries.
    // Because one CardSearchService is shared by every page, the CALLER must ignore
    // replies whose id it did not receive from its own most recent call — otherwise
    // a second live page would consume or strand this page's result.
    std::uint64_t searchPrintings(int dexNumber, const QString& setCodeFilter);

    // Search by card NAME rather than species — for a card that depicts no Pokémon
    // (a Trainer or Energy card), which has no national dex number to search by.
    // `nameQuery` is matched as a name prefix; `setCodeFilter` narrows to a set as in
    // searchPrintings (blank = any set). Same debounce/stale-guard contract: returns a
    // request id the eventual printingsReady()/printingsFailed() carries (with a
    // dexNumber of 0, since there is no species). The caller must ignore foreign ids.
    std::uint64_t searchByName(const QString& nameQuery, const QString& setCodeFilter);

    // Fetch one card image into memory (never to disk) and emit thumbnailReady().
    // A blank url or a failed/invalid fetch simply yields no signal (the row keeps
    // its placeholder). Concurrent requests for the same cardId are de-duplicated.
    void fetchThumbnail(const QString& cardId, const QString& imageUrl);

    // The in-memory set table, once loaded — backs the expansion-code picker. Empty
    // until the first search populates it; setsReady() fires when it arrives.
    const std::vector<CardSetInfo>& sets() const { return sets_; }

Q_SIGNALS:
    void printingsReady(std::uint64_t requestId, int dexNumber,
                        const std::vector<CardCandidate>& cards);
    void printingsFailed(std::uint64_t requestId, int dexNumber);
    void setsReady();
    void thumbnailReady(const QString& cardId, const QPixmap& pixmap);

private:
    struct PendingSearch {
        int dexNumber;          // 0 == a by-name search (nameQuery is then set)
        QString nameQuery;      // the card-name query, when dexNumber == 0
        QString setCodeFilter;
        std::uint64_t generation;
    };

    void ensureSetsLoading();                 // kick off the one-time /v2/sets GET
    // Adopt the persisted set table into memory. `requireFresh` demands the cache be
    // younger than kSetCacheTtl (the startup fast-path); false accepts any cache (the
    // post-fetch-failure fallback). Returns true when a non-empty table was adopted.
    bool loadSetsFromCache(bool requireFresh);
    void fetchSets(int retriesLeft);          // the GET itself, with transient-retry
    void dispatchSearch();                     // debounce/limiter gate → startCardFetch
    void startCardFetch(int dexNumber, std::uint64_t generation, const QString& url,
                        int retriesLeft);
    void pumpThumbnails();                      // drain the thumbnail queue under the limiter

    const CardCatalogApi& api_;
    CardSetCache* cache_;  // optional cross-launch set-table cache; may be null
    QNetworkAccessManager* nam_;

    // The lazily-loaded set table and its load state.
    std::vector<CardSetInfo> sets_;
    bool setsLoaded_ = false;
    bool setsLoading_ = false;

    // Search debounce + stale-guard. generation_ is the id of the most recent
    // request; a reply whose captured generation differs is stale and dropped.
    QTimer* searchDebounce_;
    std::optional<PendingSearch> pendingSearch_;
    std::uint64_t generation_ = 0;

    // The shared network-rate backstop (searches + thumbnails).
    TokenBucket limiter_;

    // Thumbnail queue: paced by the same limiter, de-duplicated by cardId.
    struct ThumbRequest {
        QString cardId;
        QString url;
    };
    QQueue<ThumbRequest> thumbQueue_;
    QSet<QString> inFlightThumbs_;
    QTimer* thumbPump_;
};

}  // namespace pokedex
