#include "gui/services/card_price_lookup_service.h"

#include <QDebug>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <chrono>

#include "core/app/card_catalog_api.h"
#include "core/app/card_price_service.h"
#include "gui/services/http_status.h"
#include "gui/services/network_log.h"

namespace pokedex {

namespace {

// How long a fetched price stays "fresh" before needsRefresh() reports it stale.
// Prices move roughly daily, but we never auto-refetch — this only labels the state
// for the user, who forces a refresh when they want the latest — so a day is ample.
constexpr auto kPriceTtl = std::chrono::hours(24);

// Per-card retry/backoff: the API 5xx/504s under load, so a transient failure is
// retried a few times with growing delays before it surfaces as pricesFailed().
constexpr int kMaxRetries = 3;
constexpr int kBackoffBaseMs = 400;

}  // namespace

CardPriceLookupService::CardPriceLookupService(const CardCatalogApi& api, CardPriceService& prices,
                                               QObject* parent)
    : QObject(parent), api_(api), prices_(prices), nam_(new QNetworkAccessManager(this)) {}

std::vector<CardPrice> CardPriceLookupService::cached(const QString& externalCardId) {
    return prices_.pricesFor(externalCardId.toStdString());
}

std::optional<Timestamp> CardPriceLookupService::fetchedAt(const QString& externalCardId) {
    return prices_.fetchedAt(externalCardId.toStdString());
}

bool CardPriceLookupService::needsRefresh(const QString& externalCardId) {
    return prices_.needsRefresh(externalCardId.toStdString(), kPriceTtl);
}

void CardPriceLookupService::fetch(const QString& externalCardId, bool force) {
    if (externalCardId.isEmpty()) {
        return;  // an unlinked copy has nothing to fetch
    }
    // A fresh cache satisfies a non-forced fetch with no network call.
    if (!force && !prices_.needsRefresh(externalCardId.toStdString(), kPriceTtl)) {
        Q_EMIT pricesReady(externalCardId);
        return;
    }
    if (inFlight_.contains(externalCardId)) {
        // Coalesce: a fetch for this id is already on the wire. Because the service is
        // app-shared and pricesReady/pricesFailed carry the id, that single fetch's
        // fresh result is delivered to EVERY panel showing this card — so a concurrent
        // (even forced) request is satisfied by it and needs no second GET.
        return;
    }
    inFlight_.insert(externalCardId);
    startFetch(externalCardId, kMaxRetries);
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
                const int delay = backoffDelayMs(retriesLeft, kMaxRetries, kBackoffBaseMs);
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
            inFlight_.remove(externalCardId);
            Q_EMIT pricesFailed(externalCardId);
            return;
        }
        try {
            prices_.recordApiPrices(externalCardId.toStdString(),
                                    reply->readAll().toStdString());
            inFlight_.remove(externalCardId);
            Q_EMIT pricesReady(externalCardId);
        } catch (const std::exception& e) {
            qWarning() << "CardPriceLookupService: could not parse prices for" << externalCardId
                       << ":" << e.what();
            inFlight_.remove(externalCardId);
            Q_EMIT pricesFailed(externalCardId);
        }
    });
}

}  // namespace pokedex
