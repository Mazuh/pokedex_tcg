#include "gui/services/card_search_service.h"

#include <QByteArray>
#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <chrono>

#include "core/app/cache_ttl.h"
#include "core/app/card_catalog_api.h"
#include "core/app/card_catalog_parse.h"
#include "core/app/card_set_cache.h"
#include "gui/services/http_status.h"
#include "gui/services/network_log.h"

namespace pokedex {

namespace {

// Debounce window: a search waits this long for the input to settle (a species
// landing, or a burst of expansion-code keystrokes) before hitting the wire.
constexpr int kDebounceMs = 200;
// When the limiter denies a due request, retry after this — the pending target is
// kept, so the request paces itself to the refill rate.
constexpr int kThrottleRetryMs = 150;
// Network budget shared by searches and thumbnails: a burst from idle, then a
// sustained ceiling. Deliberately modest — the public API is not generous, and a
// transient 429/504 is absorbed by the search retry/backoff below.
constexpr double kBurst = 8.0;
constexpr double kSustainedPerSecond = 5.0;
// Card-search retry/backoff: the API 504s under load, so a transient failure is
// retried a few times with growing delays before it surfaces as failed().
constexpr int kMaxSearchRetries = 3;
constexpr int kBackoffBaseMs = 400;
// A filter that resolves to more than this many sets isn't a useful narrow and
// would OR that many set.id clauses into the query URL (risking the API's length
// limit), so treat it as unnarrowed.
constexpr std::size_t kMaxNarrowSets = 12;
// How long a cached set table stays fresh. The set table changes only a few times a
// year (a new expansion), so a day is plenty — it means at most one /v2/sets fetch
// per day rather than one per launch, sparing the daily-flaky public API.
constexpr auto kSetCacheTtl = std::chrono::hours(24);

std::int64_t monotonicNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

}  // namespace

CardSearchService::CardSearchService(const CardCatalogApi& api, CardSetCache* cache,
                                     QObject* parent)
    : QObject(parent),
      api_(api),
      cache_(cache),
      nam_(new QNetworkAccessManager(this)),
      searchDebounce_(new QTimer(this)),
      limiter_(kBurst, kSustainedPerSecond),
      thumbPump_(new QTimer(this)) {
    searchDebounce_->setSingleShot(true);
    connect(searchDebounce_, &QTimer::timeout, this, &CardSearchService::dispatchSearch);
    thumbPump_->setSingleShot(true);
    connect(thumbPump_, &QTimer::timeout, this, &CardSearchService::pumpThumbnails);

    // Warm the set table so it is ready in memory before the user ever opens "Add
    // copy" — no wait on first use. Prefer a fresh on-disk cache (skipping the
    // network entirely); only reach for /v2/sets when the cache is missing or stale.
    // The service outlives the window, so this happens once per session.
    if (!loadSetsFromCache(/*requireFresh=*/true)) {
        ensureSetsLoading();
    }
}

std::uint64_t CardSearchService::searchPrintings(int dexNumber, const QString& setCodeFilter) {
    // Mint a unique id for this request; the reply carries it so the caller can tell
    // its own result from another page's (this service is shared app-wide).
    const std::uint64_t requestId = ++generation_;
    pendingSearch_ = PendingSearch{dexNumber, QString(), setCodeFilter, requestId};
    ensureSetsLoading();  // load the set table once, in parallel with the debounce
    searchDebounce_->start(kDebounceMs);
    return requestId;
}

std::uint64_t CardSearchService::searchByName(const QString& nameQuery,
                                              const QString& setCodeFilter) {
    // A by-name search is tagged with dexNumber 0 (no species); the rest of the
    // pipeline — debounce, set-filter resolution, stale-guard — is identical.
    const std::uint64_t requestId = ++generation_;
    pendingSearch_ = PendingSearch{0, nameQuery, setCodeFilter, requestId};
    ensureSetsLoading();
    searchDebounce_->start(kDebounceMs);
    return requestId;
}

void CardSearchService::ensureSetsLoading() {
    if (setsLoaded_ || setsLoading_) {
        return;
    }
    setsLoading_ = true;
    fetchSets(kMaxSearchRetries);
}

bool CardSearchService::loadSetsFromCache(bool requireFresh) {
    if (cache_ == nullptr) {
        return false;
    }
    // A cache read failure (corrupt/locked DB) must never break search — treat it as
    // "no cache" and let the network path take over.
    try {
        const std::optional<Timestamp> fetchedAt = cache_->fetchedAt();
        if (!fetchedAt) {
            return false;  // never fetched
        }
        // The startup fast-path demands a fresh cache (within kSetCacheTtl, and not
        // future-dated — see cacheIsFresh); the post-failure fallback accepts any age.
        if (requireFresh &&
            !cacheIsFresh(*fetchedAt, std::chrono::system_clock::now(), kSetCacheTtl)) {
            return false;  // stale (or future) — the caller will re-fetch
        }
        std::vector<CardSetInfo> cached = cache_->load();
        if (cached.empty()) {
            return false;  // an empty table is no better than no table
        }
        sets_ = std::move(cached);
        setsLoading_ = false;
        // Adopting a cache does not by itself finalize the load state: the startup
        // fast-path (requireFresh) marks the table loaded, and so does the
        // degraded-mode fallback (see fallBackToCache, which sets setsLoaded_ after a
        // successful adopt). This method only reports success; the caller decides
        // whether the adopt ends further fetching.
        qInfo().noquote() << "CardSearchService: loaded" << sets_.size()
                          << (requireFresh ? "sets from cache" : "sets from stale cache");
        if (requireFresh) {
            setsLoaded_ = true;
        }
        Q_EMIT setsReady();
        return true;
    } catch (const std::exception& e) {
        qWarning() << "CardSearchService: set cache read failed:" << e.what();
        return false;
    }
}

void CardSearchService::fallBackToCache() {
    // If a cached table is available, adopting it also ends further fetching for the
    // session (setsLoaded_): we would rather narrow from a slightly stale-but-complete
    // table than re-hit the flaky /v2/sets on every search while the API is degraded.
    // With no cache we leave setsLoaded_ false, so a later search retries — there is
    // nothing to narrow with until a real list arrives.
    if (loadSetsFromCache(/*requireFresh=*/false)) {
        setsLoaded_ = true;
    }
}

void CardSearchService::fetchSets(int retriesLeft) {
    QNetworkRequest request{QUrl(QString::fromStdString(api_.resolveSets().url))};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, retriesLeft]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            setsLoading_ = false;
            try {
                std::vector<CardSetInfo> parsed =
                    parseSetsResponse(reply->readAll().toStdString());
                if (parsed.empty()) {
                    // A 200 carrying an empty table is a degraded-mode response, not a
                    // real set list — never overwrite the last-good cache with it (that
                    // would destroy the outage fallback). Treat it like a failed fetch:
                    // fall back to the cache (even if stale) and, if we have one, stop
                    // re-fetching for the session.
                    qWarning() << "CardSearchService: set list came back empty — keeping cache";
                    fallBackToCache();
                } else {
                    sets_ = std::move(parsed);
                    setsLoaded_ = true;
                    qInfo().noquote() << "CardSearchService: loaded" << sets_.size() << "sets";
                    // Persist the fresh table so the next launch (within the TTL) can skip
                    // this fetch. A cache write failure is non-fatal — the in-memory table
                    // is already good for this session.
                    if (cache_ != nullptr) {
                        try {
                            cache_->store(sets_, std::chrono::system_clock::now());
                        } catch (const std::exception& e) {
                            qWarning() << "CardSearchService: could not cache set list:"
                                       << e.what();
                        }
                    }
                    Q_EMIT setsReady();
                }
            } catch (const std::exception& e) {
                // A parse failure likewise leaves the good cache intact and falls back
                // to it, rather than dropping set narrowing for the session.
                qWarning() << "CardSearchService: could not parse set list:" << e.what();
                fallBackToCache();
            }
        } else if (isTransient(reply) && retriesLeft > 0) {
            const int delay = backoffDelayMs(retriesLeft, kMaxSearchRetries, kBackoffBaseMs);
            qWarning().noquote() << "CardSearchService: set list" << httpStatusNote(reply)
                                 << "— retrying in" << delay << "ms (" << retriesLeft << "left)";
            QTimer::singleShot(delay, this, [this, retriesLeft]() { fetchSets(retriesLeft - 1); });
            return;  // keep setsLoading_ true across the retry
        } else {
            // Not fatal: fall back to a stale cache if we have one, so searches keep
            // narrowing from the last good table while the API is down (and, having a
            // usable table, stop re-fetching for the session). Failing that (no cache),
            // searches proceed without the table (no narrowing, and expansion codes fall
            // back to each card's embedded set) and setsLoaded_ stays false, so the next
            // search re-attempts the fetch once the API recovers.
            setsLoading_ = false;
            qWarning().noquote() << "CardSearchService: set list fetch failed —"
                                 << httpStatusNote(reply) << ":" << reply->errorString();
            fallBackToCache();
        }
        // On a terminal outcome (loaded, parse-fail, or exhausted), let any pending
        // search proceed now that the load is no longer in flight.
        if (pendingSearch_ && !searchDebounce_->isActive()) {
            dispatchSearch();
        }
    });
}

void CardSearchService::dispatchSearch() {
    if (!pendingSearch_) {
        return;
    }
    // Prefer to wait out an in-flight set load so the first results carry correct
    // expansion codes and honor a typed filter; ensureSetsLoading's completion
    // re-drives us.
    if (!setsLoaded_ && setsLoading_) {
        return;
    }

    const PendingSearch req = *pendingSearch_;

    // Resolve the typed set filter (code or name) to set ids, BEFORE spending a rate
    // token — a filter that matches nothing needs no request. This is the single
    // authority on narrowing; callers pass a raw filter and read back the results.
    CardSearchQuery query;
    if (req.dexNumber != 0) {
        query.dexNumber = req.dexNumber;  // species search
    } else {
        query.nameQuery = req.nameQuery.trimmed().toStdString();  // by-name search
        if (query.nameQuery.empty()) {
            // A blank name has no scope clause — never fall back to fetching the whole
            // catalog (resolveSearch would build an empty `q=`). Emit no results, as the
            // empty-set-filter branch below does. (The finder gates on 3+ chars, so this
            // is defense for the public searchByName seam.)
            pendingSearch_.reset();
            Q_EMIT printingsReady(req.generation, req.dexNumber, {});
            return;
        }
    }
    const std::string filter = req.setCodeFilter.trimmed().toStdString();
    if (!filter.empty()) {
        query.setIds = resolveSetFilterToIds(filter, sets_);
        if (query.setIds.empty()) {
            // A non-empty filter that matches no set (or was typed before the set
            // table loaded) yields NO cards — never fall back to fetching every
            // printing of the species. Emit an empty result for this request.
            pendingSearch_.reset();
            Q_EMIT printingsReady(req.generation, req.dexNumber, {});
            return;
        }
        if (query.setIds.size() > kMaxNarrowSets) {
            // Too broad to send as one query URL; cap it (still narrowed — never the
            // whole species) so the user sees something and can refine.
            query.setIds.resize(kMaxNarrowSets);
        }
    }

    if (!limiter_.tryAcquire(monotonicNowMs())) {
        searchDebounce_->start(kThrottleRetryMs);  // keep pendingSearch_ for the retry
        return;
    }
    pendingSearch_.reset();
    startCardFetch(req.dexNumber, req.generation,
                   QString::fromStdString(api_.resolveSearch(query).url), kMaxSearchRetries);
}

void CardSearchService::startCardFetch(int dexNumber, std::uint64_t generation,
                                       const QString& url, int retriesLeft) {
    QNetworkRequest request{QUrl(url)};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, dexNumber, generation, url, retriesLeft]() {
                reply->deleteLater();

                // Every reply is emitted tagged with its request id (`generation`);
                // routing to the right page is the caller's job — we do NOT drop by a
                // global "latest" counter, since that would strand another live page's
                // in-flight request whenever a second page searched.
                if (reply->error() != QNetworkReply::NoError) {
                    if (isTransient(reply) && retriesLeft > 0) {
                        const int delay =
                            backoffDelayMs(retriesLeft, kMaxSearchRetries, kBackoffBaseMs);
                        qWarning().noquote()
                            << "CardSearchService: dex" << dexNumber << httpStatusNote(reply)
                            << "— retrying in" << delay << "ms (" << retriesLeft << "left)";
                        QTimer::singleShot(delay, this, [this, dexNumber, generation, url,
                                                         retriesLeft]() {
                            startCardFetch(dexNumber, generation, url, retriesLeft - 1);
                        });
                        return;
                    }
                    qWarning().noquote()
                        << "CardSearchService: search failed for dex" << dexNumber << "—"
                        << httpStatusNote(reply) << ":" << reply->errorString()
                        << "| url:" << url;
                    Q_EMIT printingsFailed(generation, dexNumber);
                    return;
                }

                try {
                    const std::vector<CardCandidate> cards =
                        parseCardSearchResponse(reply->readAll().toStdString(), sets_);
                    Q_EMIT printingsReady(generation, dexNumber, cards);
                } catch (const std::exception& e) {
                    qWarning() << "CardSearchService: could not parse search results for dex"
                               << dexNumber << ":" << e.what();
                    Q_EMIT printingsFailed(generation, dexNumber);
                }
            });
}

void CardSearchService::fetchThumbnail(const QString& cardId, const QString& imageUrl) {
    // A blank id can't key the in-flight set (an empty "" would block every other
    // id-less thumbnail), and a blank url has nothing to fetch.
    if (cardId.isEmpty() || imageUrl.isEmpty() || inFlightThumbs_.contains(cardId)) {
        return;
    }
    thumbQueue_.enqueue(ThumbRequest{cardId, imageUrl});
    pumpThumbnails();
}

void CardSearchService::pumpThumbnails() {
    if (thumbQueue_.isEmpty()) {
        return;
    }
    if (!limiter_.tryAcquire(monotonicNowMs())) {
        thumbPump_->start(kThrottleRetryMs);
        return;
    }

    const ThumbRequest req = thumbQueue_.dequeue();
    inFlightThumbs_.insert(req.cardId);

    QNetworkRequest request{QUrl(req.url)};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cardId = req.cardId]() {
        reply->deleteLater();
        inFlightThumbs_.remove(cardId);
        if (reply->error() == QNetworkReply::NoError) {
            QPixmap pixmap;
            const QByteArray bytes = reply->readAll();
            // Held in memory only — never written to disk (the no-cache rule).
            if (!bytes.isEmpty() && pixmap.loadFromData(bytes)) {
                Q_EMIT thumbnailReady(cardId, pixmap);
            }
        }
        // Whether this one succeeded or not, keep draining the queue.
        pumpThumbnails();
    });

    // Fire additional fetches this tick until the burst budget is spent, so a
    // freshly loaded chunk of rows fills in promptly.
    pumpThumbnails();
}

}  // namespace pokedex
