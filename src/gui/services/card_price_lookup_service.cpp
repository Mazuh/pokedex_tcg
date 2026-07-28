#include "gui/services/card_price_lookup_service.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <chrono>

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

void CardPriceLookupService::finishSucceeded(const QString& externalCardId) {
    inFlight_.remove(externalCardId);
    // The fresh result is delivered to every waiting panel, so a coalesced Refresh is
    // already satisfied — drop it rather than re-fetch.
    refetchQueued_.remove(externalCardId);
    Q_EMIT pricesReady(externalCardId);
}

void CardPriceLookupService::finishFailed(const QString& externalCardId) {
    inFlight_.remove(externalCardId);
    if (refetchQueued_.remove(externalCardId)) {
        // An explicit Fetch/Refresh coalesced onto this now-failed attempt. Honor it with
        // a real fresh fetch (full retry budget) instead of reporting the inherited
        // failure. Only one re-issue per queued click, so a persistently-failing API
        // cannot loop: this re-fetch fails with an empty queue → pricesFailed.
        inFlight_.insert(externalCardId);
        startFetch(externalCardId, kApiMaxRetries);
        return;
    }
    Q_EMIT pricesFailed(externalCardId);
}

void CardPriceLookupService::startFetch(const QString& externalCardId, int retriesLeft) {
    QNetworkRequest request{QUrl(tcgdexCardUrl(externalCardId))};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, externalCardId, retriesLeft]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // A 404 here is terminal, not retryable: the card id is genuinely absent from the
            // provider (unlike a search, where 404 is a spurious flake) — see http_status.h.
            if (isTransient(reply, /*retry404=*/false) && retriesLeft > 0) {
                const int delay = backoffDelayMs(retriesLeft, kApiMaxRetries, kApiBackoffBaseMs);
                qWarning().noquote()
                    << "CardPriceLookupService: prices for" << externalCardId
                    << httpStatusNote(reply) << "— retrying in" << delay << "ms ("
                    << retriesLeft << "left)";
                QTimer::singleShot(delay, this, [this, externalCardId, retriesLeft]() {
                    startFetch(externalCardId, retriesLeft - 1);
                });
                return;  // keep the in-flight marker across the retry
            }
            qWarning().noquote() << "CardPriceLookupService: price fetch failed for"
                                 << externalCardId << "—" << httpStatusNote(reply) << ":"
                                 << reply->errorString();
            finishFailed(externalCardId);
            return;
        }
        try {
            const auto recorded = prices_.recordTcgdexPrices(externalCardId.toStdString(),
                                                             reply->readAll().toStdString());
            if (recorded.degraded) {
                // A 200 with no card object (an error body the provider returns instead of a
                // 5xx): nothing was stored or stamped, so this is a failed fetch, not a "no
                // prices" answer. Report it as a failure so the panel shows a retryable error
                // rather than silently no-op'ing on a Fetch click.
                qWarning() << "CardPriceLookupService: degraded price response (no card) for"
                           << externalCardId;
                finishFailed(externalCardId);
            } else {
                finishSucceeded(externalCardId);
            }
        } catch (const std::exception& e) {
            qWarning() << "CardPriceLookupService: could not parse prices for" << externalCardId
                       << ":" << e.what();
            finishFailed(externalCardId);
        }
    });
}

}  // namespace pokedex
