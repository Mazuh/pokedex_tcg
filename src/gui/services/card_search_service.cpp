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

std::int64_t monotonicNowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

// A transient failure worth retrying: any network-layer error, or an HTTP 5xx /
// 429. A 4xx (other than 429) is a client error we should not hammer.
bool isTransient(QNetworkReply* reply) {
    if (reply->error() == QNetworkReply::NoError) {
        return false;
    }
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid()) {
        return true;  // no HTTP status → a network-layer error (timeout, DNS, reset)
    }
    const int code = status.toInt();
    return code == 429 || code >= 500;
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
}

void CardSearchService::searchPrintings(int dexNumber, const QString& setCodeFilter) {
    // Bump the generation so any in-flight reply for an earlier request is now stale
    // and will be dropped when it returns.
    pendingSearch_ = PendingSearch{dexNumber, setCodeFilter, ++generation_};
    ensureSetsLoading();  // load the set table once, in parallel with the debounce
    searchDebounce_->start(kDebounceMs);
}

void CardSearchService::ensureSetsLoading() {
    if (setsLoaded_ || setsLoading_) {
        return;
    }
    setsLoading_ = true;
    QNetworkRequest request{QUrl(QString::fromStdString(api_.resolveSets().url))};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        setsLoading_ = false;
        if (reply->error() == QNetworkReply::NoError) {
            try {
                sets_ = parseSetsResponse(reply->readAll().toStdString());
                setsLoaded_ = true;
                Q_EMIT setsReady();
            } catch (const std::exception& e) {
                qWarning() << "CardSearchService: could not parse set list:" << e.what();
            }
        } else {
            // Not fatal: searches proceed without the table (no narrowing, and
            // expansion codes fall back to each card's embedded set). The next
            // search will attempt the load again, since setsLoaded_ stays false.
            qWarning() << "CardSearchService: set list fetch failed:" << reply->errorString();
        }
        // Whether it loaded, failed, or a search was already waiting, let the
        // pending search proceed now that the load is no longer in flight.
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
    if (!limiter_.tryAcquire(monotonicNowMs())) {
        searchDebounce_->start(kThrottleRetryMs);
        return;
    }

    const PendingSearch req = *pendingSearch_;
    pendingSearch_.reset();

    CardSearchQuery query;
    query.dexNumber = req.dexNumber;
    if (!req.setCodeFilter.trimmed().isEmpty()) {
        // Resolve the typed printed code to set ids. An in-progress code that
        // matches nothing yet resolves to no ids → search unnarrowed rather than
        // showing an empty list mid-typing.
        query.setIds = resolveSetCodeToIds(req.setCodeFilter.toStdString(), sets_);
    }
    startCardFetch(req.dexNumber, req.generation,
                   QString::fromStdString(api_.resolveSearch(query).url), kMaxSearchRetries);
}

void CardSearchService::startCardFetch(int dexNumber, std::uint64_t generation,
                                       const QString& url, int retriesLeft) {
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, dexNumber, generation, url, retriesLeft]() {
                reply->deleteLater();

                // Stale-guard: a newer search superseded this one (different species
                // or filter), so its result is no longer wanted.
                if (generation != generation_) {
                    return;
                }

                if (reply->error() != QNetworkReply::NoError) {
                    if (isTransient(reply) && retriesLeft > 0) {
                        const int attempt = kMaxSearchRetries - retriesLeft;
                        const int delay = kBackoffBaseMs * (1 << attempt);  // 400, 800, 1600…
                        QTimer::singleShot(delay, this, [this, dexNumber, generation, url,
                                                         retriesLeft]() {
                            if (generation == generation_) {
                                startCardFetch(dexNumber, generation, url, retriesLeft - 1);
                            }
                        });
                        return;
                    }
                    qWarning() << "CardSearchService: search failed for dex" << dexNumber << ":"
                               << reply->errorString();
                    Q_EMIT printingsFailed(dexNumber);
                    return;
                }

                try {
                    const std::vector<CardCandidate> cards =
                        parseCardSearchResponse(reply->readAll().toStdString(), sets_);
                    Q_EMIT printingsReady(dexNumber, cards);
                } catch (const std::exception& e) {
                    qWarning() << "CardSearchService: could not parse search results for dex"
                               << dexNumber << ":" << e.what();
                    Q_EMIT printingsFailed(dexNumber);
                }
            });
}

void CardSearchService::fetchThumbnail(const QString& cardId, const QString& imageUrl) {
    if (imageUrl.isEmpty() || inFlightThumbs_.contains(cardId)) {
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
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(request);
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
