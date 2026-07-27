#include "gui/services/card_price_lookup_service.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include "core/app/card_catalog_api.h"
#include "core/app/card_price_service.h"
#include "gui/services/http_status.h"
#include "gui/services/network_log.h"

namespace pokedex {

CardPriceLookupService::CardPriceLookupService(const CardCatalogApi& api, CardPriceService& prices,
                                               QObject* parent)
    : QObject(parent), api_(api), prices_(prices), nam_(new QNetworkAccessManager(this)) {}

CardPriceLookupService::CachedPrices CardPriceLookupService::cachedPrices(
    const QString& externalCardId) {
    const std::string id = externalCardId.toStdString();
    return {prices_.pricesFor(id), prices_.fetchedAt(id)};
}

std::unordered_map<std::string, std::vector<CardPrice>> CardPriceLookupService::cachedMany(
    const std::vector<std::string>& externalCardIds) {
    return prices_.pricesForMany(externalCardIds);
}

void CardPriceLookupService::fetch(const QString& externalCardId) {
    if (externalCardId.isEmpty()) {
        return;  // an unlinked copy has nothing to fetch
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
    QNetworkRequest request{
        QUrl(QString::fromStdString(api_.resolveCardById(externalCardId.toStdString()).url))};
    QNetworkReply* reply = loggedGet(nam_, request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, externalCardId, retriesLeft]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // A 404 here is terminal, not retryable: the card id is genuinely absent
            // (unlike a search, where 404 is a spurious flake) — see http_status.h.
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
            const auto recorded = prices_.recordApiPrices(externalCardId.toStdString(),
                                                           reply->readAll().toStdString());
            if (recorded.degraded) {
                // A 200 with no card object (data:null / an error body the API returns
                // instead of a 5xx): nothing was stored or stamped, so this is a failed
                // fetch, not a "no prices" answer. Report it as a failure so the panel
                // shows a retryable error rather than silently no-op'ing on a Fetch click.
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
