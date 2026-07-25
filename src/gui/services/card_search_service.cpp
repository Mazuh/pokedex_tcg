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

#include "core/app/card_catalog_api.h"
#include "core/app/card_catalog_parse.h"
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

std::int64_t monotonicNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// A transient failure worth retrying: any network-layer error, or an HTTP 429 /
// 5xx / 404. 404 counts because pokemontcg.io intermittently returns spurious
// 404s (and 504s) under load for perfectly valid queries — a search never has a
// legitimate 404 (no results is a 200 with an empty list), and an immediate retry
// typically succeeds. A 4xx other than 404 is a real client error we don't hammer.
bool isTransient(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        return false;
    }
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid()) {
        return true;  // no HTTP status → a network-layer error (timeout, DNS, reset)
    }
    const int code = status.toInt();
    return code == 404 || code == 429 || code >= 500;
}

// A human-readable one-liner naming the HTTP status (and what it usually means for
// this API), so the logs distinguish rate-limiting from the API just flaking.
QString httpStatusNote(QNetworkReply* reply) {
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid()) {
        return QStringLiteral("network error (no HTTP status)");
    }
    const int code = status.toInt();
    QString meaning;
    switch (code) {
        case 404: meaning = QStringLiteral("spurious — API flaking, retryable"); break;
        case 429: meaning = QStringLiteral("RATE LIMITED"); break;
        case 500:
        case 502:
        case 503: meaning = QStringLiteral("server error"); break;
        case 504: meaning = QStringLiteral("gateway timeout — API busy"); break;
        default: meaning = (code >= 200 && code < 300) ? QStringLiteral("ok")
                                                       : QStringLiteral("error");
    }
    return QStringLiteral("HTTP %1 (%2)").arg(code).arg(meaning);
}

}  // namespace

CardSearchService::CardSearchService(const CardCatalogApi& api, QObject* parent)
    : QObject(parent),
      api_(api),
      nam_(new QNetworkAccessManager(this)),
      searchDebounce_(new QTimer(this)),
      limiter_(kBurst, kSustainedPerSecond),
      thumbPump_(new QTimer(this)) {
    searchDebounce_->setSingleShot(true);
    connect(searchDebounce_, &QTimer::timeout, this, &CardSearchService::dispatchSearch);
    thumbPump_->setSingleShot(true);
    connect(thumbPump_, &QTimer::timeout, this, &CardSearchService::pumpThumbnails);

    // Warm the set table immediately (the GET dispatches once the event loop runs),
    // so it is cached in memory before the user ever opens "Add copy" — no wait on
    // first use. The service outlives the window, so this happens once per session.
    ensureSetsLoading();
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

void CardSearchService::fetchSets(int retriesLeft) {
    QNetworkRequest request{QUrl(QString::fromStdString(api_.resolveSets().url))};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, retriesLeft]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            setsLoading_ = false;
            try {
                sets_ = parseSetsResponse(reply->readAll().toStdString());
                setsLoaded_ = true;
                qInfo().noquote() << "CardSearchService: loaded" << sets_.size() << "sets";
                Q_EMIT setsReady();
            } catch (const std::exception& e) {
                qWarning() << "CardSearchService: could not parse set list:" << e.what();
            }
        } else if (isTransient(reply) && retriesLeft > 0) {
            const int attempt = kMaxSearchRetries - retriesLeft;
            const int delay = kBackoffBaseMs * (1 << attempt);
            qWarning().noquote() << "CardSearchService: set list" << httpStatusNote(reply)
                                 << "— retrying in" << delay << "ms (" << retriesLeft << "left)";
            QTimer::singleShot(delay, this, [this, retriesLeft]() { fetchSets(retriesLeft - 1); });
            return;  // keep setsLoading_ true across the retry
        } else {
            // Not fatal: searches proceed without the table (no narrowing, and
            // expansion codes fall back to each card's embedded set). The next
            // search will attempt the load again, since setsLoaded_ stays false.
            setsLoading_ = false;
            qWarning().noquote() << "CardSearchService: set list fetch failed —"
                                 << httpStatusNote(reply) << ":" << reply->errorString();
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
                        const int attempt = kMaxSearchRetries - retriesLeft;
                        const int delay = kBackoffBaseMs * (1 << attempt);  // 400, 800, 1600…
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
