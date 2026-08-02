#include "gui/services/card_price_lookup_service.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QVariant>

#include <chrono>
#include <optional>

#include "core/app/cache_ttl.h"
#include "core/app/card_catalog_parse.h"
#include "core/app/card_price_service.h"
#include "core/app/card_set_cache.h"
#include "gui/services/http_status.h"
#include "gui/services/network_log.h"
#include "gui/services/set_cache_read.h"

namespace pokedex {

namespace {

// The tcgdex REST base (English). A card is addressed by its tcgdex id ("mep-013") at
// /cards/{id}; the set table is the flat /sets array. See card_catalog_parse for the parsers.
constexpr char kTcgdexBase[] = "https://api.tcgdex.net/v2/en";

QString tcgdexCardUrl(const QString& cardId) {
    return QString::fromLatin1(kTcgdexBase) + QStringLiteral("/cards/") +
           QString::fromUtf8(QUrl::toPercentEncoding(cardId));
}

QString tcgdexSetsUrl() { return QString::fromLatin1(kTcgdexBase) + QStringLiteral("/sets"); }

// The set-id part of a tcgdex card id ("me02-055" → "me02"), or nullopt if it has no "-".
std::optional<QString> dashPrefix(const QString& cardId) {
    const int dash = cardId.lastIndexOf(QLatin1Char('-'));
    if (dash <= 0) {
        return std::nullopt;
    }
    return cardId.left(dash);
}

// A zero-padded (width-3) variant of a tcgdex card id, or nullopt when there's nothing to pad.
// tcgdex addresses a card as setId "-" localId; for modern sets the localId is the collector
// number zero-padded to three digits ("me02-055"), but older sets leave it unpadded ("base1-4"),
// and nothing in the set metadata distinguishes the two. So we can't build the right id up front:
// we fetch the number as printed and, on a 404, retry this padded form. Returns nullopt when
// there is nothing to pad — a non-numeric localId (a promo like "swsh12-tg01" or "TG05"), an
// already-3+-digit number, or an id with no "-" — so those cards keep their id unchanged.
std::optional<QString> paddedCardIdCandidate(const QString& cardId) {
    const int dash = cardId.lastIndexOf(QLatin1Char('-'));
    if (dash <= 0 || dash >= cardId.size() - 1) {
        return std::nullopt;
    }
    const QString localId = cardId.mid(dash + 1);
    if (localId.size() >= 3) {
        return std::nullopt;
    }
    for (const QChar c : localId) {
        if (!c.isDigit()) {
            return std::nullopt;
        }
    }
    return cardId.left(dash + 1) + QString(3 - localId.size(), QLatin1Char('0')) + localId;
}

}  // namespace

CardPriceLookupService::CardPriceLookupService(CardPriceService& prices, CardSetCache* setCache,
                                               QObject* parent)
    : QObject(parent),
      prices_(prices),
      setCache_(setCache),
      nam_(new QNetworkAccessManager(this)) {}

CardPriceLookupService::CachedPrices CardPriceLookupService::cachedPrices(
    const QString& externalCardId) {
    const std::string id = externalCardId.toStdString();
    return {prices_.pricesFor(id), prices_.fetchedAt(id)};
}

std::unordered_map<std::string, std::vector<CardPrice>> CardPriceLookupService::cachedMany(
    const std::vector<std::string>& externalCardIds) {
    return prices_.pricesForMany(externalCardIds);
}

bool CardPriceLookupService::pricesFresh(const QString& externalCardId) {
    if (externalCardId.isEmpty()) {
        return false;
    }
    const std::optional<Timestamp> fetchedAt = prices_.fetchedAt(externalCardId.toStdString());
    if (!fetchedAt) {
        return false;  // never fetched — an auto-fetch should populate it
    }
    // The same freshness rule (incl. the backward-clock guard) the caches share; a day matches
    // the set-table TTL and is plenty for "was this card just priced by an earlier add".
    constexpr auto kAutoFetchTtl = std::chrono::hours(24);
    return cacheIsFresh(*fetchedAt, std::chrono::system_clock::now(), kAutoFetchTtl);
}

std::optional<QString> CardPriceLookupService::resolveTcgdexId(const CardReference& ref) const {
    if (!tcgdexSetsLoaded_) {
        return std::nullopt;  // no table yet — caller must ensureTcgdexSets() first
    }
    if (const auto id = resolveTcgdexCardId(ref, tcgdexSets_)) {
        return QString::fromStdString(*id);
    }
    return std::nullopt;
}

void CardPriceLookupService::ensureTcgdexSets(bool forceRefresh) {
    if (tcgdexSetsLoaded_ && !forceRefresh) {
        Q_EMIT tcgdexSetsResolved(true);  // already in memory — no disk read, no network
        return;
    }
    if (tcgdexSetsFetching_) {
        return;  // a fetch is on the wire; its completion will emit for every waiting caller
    }
    // Prefer a fresh disk cache (no network) — the common case after the first fetch, even
    // across launches while the table stays within the TTL. A forceRefresh skips this so a
    // resolve miss can pick up a newly-published set the cached copy predates.
    if (!forceRefresh && loadSetsFromCache(/*requireFresh=*/true)) {
        Q_EMIT tcgdexSetsResolved(true);
        return;
    }
    tcgdexSetsFetching_ = true;
    startSetsFetch(kApiMaxRetries);
}

bool CardPriceLookupService::loadSetsFromCache(bool requireFresh) {
    if (setCache_ == nullptr) {
        return false;
    }
    try {
        std::optional<std::vector<CardSetInfo>> cached = readSetCache(*setCache_, requireFresh);
        if (!cached) {
            return false;
        }
        tcgdexSets_ = std::move(*cached);
        tcgdexSetsLoaded_ = true;
        return true;
    } catch (const std::exception& e) {
        qWarning() << "CardPriceLookupService: reading the tcgdex set cache failed:" << e.what();
        return false;
    }
}

void CardPriceLookupService::startSetsFetch(int retriesLeft) {
    QNetworkRequest request{QUrl(tcgdexSetsUrl())};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, retriesLeft]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (isTransient(reply, /*retry404=*/false) && retriesLeft > 0) {
                const int delay = backoffDelayMs(retriesLeft, kApiMaxRetries, kApiBackoffBaseMs);
                QTimer::singleShot(delay, this,
                                   [this, retriesLeft]() { startSetsFetch(retriesLeft - 1); });
                return;
            }
            qWarning().noquote() << "CardPriceLookupService: tcgdex set table fetch failed —"
                                 << httpStatusNote(reply) << ":" << reply->errorString();
            tcgdexSetsFetching_ = false;
            // The API is down — narrow from a stale cached copy (any age) if we have one, so
            // pricing survives an outage. Mirrors CardSearchService's fallback.
            const bool haveStale = loadSetsFromCache(/*requireFresh=*/false);
            Q_EMIT tcgdexSetsResolved(haveStale);
            return;
        }
        std::vector<CardSetInfo> parsed;
        try {
            parsed = parseTcgdexSets(reply->readAll().toStdString());
        } catch (const std::exception& e) {
            qWarning() << "CardPriceLookupService: could not parse tcgdex set table:" << e.what();
            tcgdexSetsFetching_ = false;
            Q_EMIT tcgdexSetsResolved(loadSetsFromCache(/*requireFresh=*/false));
            return;
        }
        tcgdexSetsFetching_ = false;
        // A syntactically valid but EMPTY table is a degraded response (a partial outage
        // 200-ing an empty array), not a real "no sets" answer — so, like the network- and
        // parse-error branches, fall back to a stale cached copy rather than blanking a user
        // who has a good set table on disk.
        if (parsed.empty()) {
            Q_EMIT tcgdexSetsResolved(loadSetsFromCache(/*requireFresh=*/false));
            return;
        }
        tcgdexSets_ = std::move(parsed);
        tcgdexSetsLoaded_ = true;
        if (setCache_ != nullptr) {
            try {
                setCache_->store(tcgdexSets_, std::chrono::system_clock::now());
            } catch (const std::exception& e) {
                qWarning() << "CardPriceLookupService: persisting the tcgdex set cache failed:"
                           << e.what();  // non-fatal: the in-memory table still works this session
            }
        }
        Q_EMIT tcgdexSetsResolved(true);
    });
}

void CardPriceLookupService::fetch(const QString& externalCardId) {
    if (externalCardId.isEmpty()) {
        return;  // an unresolved copy has nothing to fetch
    }
    if (inFlight_.contains(externalCardId)) {
        // A fetch for this id is already on the wire. Because the service is app-shared
        // and pricesReady/pricesFailed carry the id, that single fetch's fresh result is
        // delivered to EVERY panel showing this card — so a concurrent request normally
        // rides along and needs no second GET. But every fetch() is an explicit user
        // Fetch/Refresh, and the in-flight attempt may be on its last retry and about to
        // fail; that click must not silently inherit the failure. Record it so a failed
        // in-flight fetch is re-issued as a genuine fresh attempt (see finishFailed); a
        // successful one still satisfies it, so we don't double-GET the common case.
        refetchQueued_.insert(externalCardId);
        return;
    }
    inFlight_.insert(externalCardId);
    startFetch(externalCardId, kApiMaxRetries);
}

void CardPriceLookupService::clearPrices(const QString& externalCardId) {
    if (externalCardId.isEmpty()) {
        return;
    }
    prices_.clearPrices(externalCardId.toStdString());
    // The cache changed (emptied) for this id — tell every view to re-read it, exactly as a
    // fetch does. They will render the not-fetched state now that nothing is cached.
    Q_EMIT pricesReady(externalCardId);
}

void CardPriceLookupService::suppressVendor(const QString& externalCardId,
                                            const QString& provenance) {
    if (externalCardId.isEmpty()) {
        return;
    }
    prices_.suppressVendor(externalCardId.toStdString(), provenance.toStdString());
    // The visible spread changed for this id (a vendor is now hidden) — re-render every view.
    Q_EMIT pricesReady(externalCardId);
}

void CardPriceLookupService::unsuppressVendor(const QString& externalCardId,
                                              const QString& provenance) {
    if (externalCardId.isEmpty()) {
        return;
    }
    prices_.unsuppressVendor(externalCardId.toStdString(), provenance.toStdString());
    Q_EMIT pricesReady(externalCardId);
}

std::vector<std::string> CardPriceLookupService::suppressedVendors(const QString& externalCardId) {
    return prices_.suppressedVendors(externalCardId.toStdString());
}

std::unordered_map<std::string, std::vector<std::string>>
CardPriceLookupService::suppressedVendorsMany(const std::vector<std::string>& externalCardIds) {
    return prices_.suppressedVendorsForMany(externalCardIds);
}

void CardPriceLookupService::finishSucceeded(const QString& externalCardId) {
    inFlight_.remove(externalCardId);
    // The fresh result is delivered to every waiting panel, so a coalesced Refresh is
    // already satisfied — drop it rather than re-fetch.
    refetchQueued_.remove(externalCardId);
    Q_EMIT pricesReady(externalCardId);
    advanceBulk(externalCardId);  // a real fetch settled — advance any bulk waiting on it
}

void CardPriceLookupService::finishFailed(const QString& externalCardId) {
    inFlight_.remove(externalCardId);
    if (refetchQueued_.remove(externalCardId)) {
        // An explicit Fetch/Refresh coalesced onto this now-failed attempt. Honor it with
        // a real fresh fetch (full retry budget) instead of reporting the inherited
        // failure. Only one re-issue per queued click, so a persistently-failing API
        // cannot loop: this re-fetch fails with an empty queue → pricesFailed. The bulk id
        // (if any) stays in flight — the re-issued fetch will settle it.
        inFlight_.insert(externalCardId);
        startFetch(externalCardId, kApiMaxRetries);
        return;
    }
    Q_EMIT pricesFailed(externalCardId);
    advanceBulk(externalCardId);  // a real fetch settled (terminally) — advance the bulk
}

void CardPriceLookupService::refreshMany(const std::vector<std::string>& externalCardIds) {
    if (bulkRunning() || externalCardIds.empty()) {
        return;  // one bulk at a time (a single shared queue), and nothing to do for an empty set
    }
    bulkPending_.assign(externalCardIds.begin(), externalCardIds.end());
    bulkInFlight_.clear();
    bulkTotal_ = static_cast<int>(bulkPending_.size());
    bulkDone_ = 0;
    Q_EMIT bulkProgress(bulkDone_, bulkTotal_);
    pumpBulk();
}

void CardPriceLookupService::pumpBulk() {
    // Keep at most kBulkMaxConcurrent bulk fetches on the wire. fetch() runs its normal retry +
    // coalescing; each id settles once via finishSucceeded/finishFailed → advanceBulk.
    while (bulkInFlight_.size() < kBulkMaxConcurrent && !bulkPending_.empty()) {
        const QString id = QString::fromStdString(bulkPending_.back());
        bulkPending_.pop_back();
        bulkInFlight_.insert(id);
        fetch(id);
    }
}

void CardPriceLookupService::advanceBulk(const QString& externalCardId) {
    if (!bulkInFlight_.remove(externalCardId)) {
        return;  // not part of the running bulk (a plain single Fetch, or no bulk running)
    }
    ++bulkDone_;
    Q_EMIT bulkProgress(bulkDone_, bulkTotal_);
    if (bulkPending_.empty() && bulkInFlight_.isEmpty()) {
        bulkTotal_ = 0;  // back to not-running (bulkRunning())
        bulkDone_ = 0;
        Q_EMIT bulkFinished();
        return;
    }
    pumpBulk();  // top the in-flight set back up
}

void CardPriceLookupService::startFetch(const QString& externalCardId, int retriesLeft) {
    // Pick the initial request URL. If we've already learned this set zero-pads its collector
    // numbers (below), go straight to the padded id so we don't respend a 404 rediscovering it
    // — the rest of a bulk refresh for the same set benefits. Otherwise fetch the id as printed;
    // startFetchUrl falls back to the padded form on a 404.
    QString urlId = externalCardId;
    const int dash = externalCardId.lastIndexOf(QLatin1Char('-'));
    if (dash > 0 && paddingSets_.contains(externalCardId.left(dash))) {
        if (const auto padded = paddedCardIdCandidate(externalCardId)) {
            urlId = *padded;
        }
    }
    startFetchUrl(externalCardId, urlId, retriesLeft, /*triedAlternate=*/false);
}

void CardPriceLookupService::startFetchUrl(const QString& externalCardId, const QString& urlId,
                                           int retriesLeft, bool triedAlternate) {
    QNetworkRequest request{QUrl(tcgdexCardUrl(urlId))};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, externalCardId, urlId, retriesLeft, triedAlternate]() {
                reply->deleteLater();
                if (reply->error() != QNetworkReply::NoError) {
                    // A 429/5xx/network error is transient — retry the SAME url. A 404 is
                    // terminal here (unlike a search, where 404 is a spurious flake — see
                    // http_status.h): the id is genuinely absent, so retry404=false skips the
                    // ladder.
                    if (isTransient(reply, /*retry404=*/false) && retriesLeft > 0) {
                        const int delay =
                            backoffDelayMs(retriesLeft, kApiMaxRetries, kApiBackoffBaseMs);
                        qWarning().noquote()
                            << "CardPriceLookupService: prices for" << urlId << httpStatusNote(reply)
                            << "— retrying in" << delay << "ms (" << retriesLeft << "left)";
                        QTimer::singleShot(delay, this,
                                           [this, externalCardId, urlId, retriesLeft, triedAlternate]() {
                                               startFetchUrl(externalCardId, urlId, retriesLeft - 1,
                                                             triedAlternate);
                                           });
                        return;  // keep the in-flight marker across the retry
                    }
                    // A terminal 404: the collector number may just be padded differently than we
                    // tried. tcgdex zero-pads it in a modern set's card id ("me02-055") but not in
                    // an older one ("base1-4"), and nothing in the set metadata says which — so try
                    // the OTHER form once (printed ↔ zero-padded, whichever we didn't send). This is
                    // symmetric on purpose: a request pre-padded because the set was learned to pad
                    // (see startFetch) still falls back to the printed id, so a card can't be
                    // stranded by a wrong guess. externalCardId (the link + cache key) is unchanged;
                    // only the URL differs, and a success still records under externalCardId.
                    const QVariant status =
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
                    if (status.toInt() == 404 && !triedAlternate) {
                        const QString alternate =
                            urlId == externalCardId
                                ? paddedCardIdCandidate(externalCardId).value_or(QString())
                                : externalCardId;
                        if (!alternate.isEmpty() && alternate != urlId) {
                            qWarning().noquote()
                                << "CardPriceLookupService: prices for" << urlId
                                << "— HTTP 404; retrying as" << alternate;
                            startFetchUrl(externalCardId, alternate, kApiMaxRetries,
                                          /*triedAlternate=*/true);
                            return;  // keep the in-flight marker across the alternate retry
                        }
                    }
                    qWarning().noquote() << "CardPriceLookupService: price fetch failed for"
                                         << externalCardId << "—" << httpStatusNote(reply) << ":"
                                         << reply->errorString();
                    finishFailed(externalCardId);
                    return;
                }
                // A zero-padded URL is what served the card: remember this set pads so its other
                // cards skip the wasted 404 (see startFetch).
                if (urlId != externalCardId && dashPrefix(externalCardId)) {
                    paddingSets_.insert(*dashPrefix(externalCardId));
                }
                try {
                    const auto recorded = prices_.recordTcgdexPrices(
                        externalCardId.toStdString(), reply->readAll().toStdString());
                    if (recorded.degraded) {
                        // A 200 with no card object (an error body the provider returns instead
                        // of a 5xx): nothing was stored or stamped, so this is a failed fetch, not
                        // a "no prices" answer. Report it as a failure so the panel shows a
                        // retryable error rather than silently no-op'ing on a Fetch click.
                        qWarning()
                            << "CardPriceLookupService: degraded price response (no card) for"
                            << externalCardId;
                        finishFailed(externalCardId);
                    } else {
                        finishSucceeded(externalCardId);
                    }
                } catch (const std::exception& e) {
                    qWarning() << "CardPriceLookupService: could not parse prices for"
                               << externalCardId << ":" << e.what();
                    finishFailed(externalCardId);
                }
            });
}

}  // namespace pokedex
