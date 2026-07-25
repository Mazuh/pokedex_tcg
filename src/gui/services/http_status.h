#pragma once

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QVariant>

namespace pokedex {

// GUI — shared HTTP-outcome helpers for the card-catalog transports (search and
// price lookup), so the two do not hand-copy the same retry classification (a
// duplication a review flagged). Both talk to the same daily-flaky pokemontcg.io API
// and want the same "is this worth retrying?" rule and the same human-readable
// status note in the logs.

// A transient failure worth retrying: any network-layer error, or an HTTP 429 / 5xx
// (and, when `retry404` is set, 404). 404 is retryable for SEARCH because
// pokemontcg.io intermittently returns spurious 404s under load for perfectly valid
// queries (a real no-results is a 200 with an empty list), so an immediate retry
// typically succeeds. But for a single-card GET (/v2/cards/{id}) a 404 means the card
// id is genuinely absent (a removed/stale link) — retrying only wastes the rate
// budget and delays the failure — so that caller passes retry404 = false. A 4xx other
// than 404 is a real client error we never hammer.
inline bool isTransient(QNetworkReply* reply, bool retry404 = true) {
    if (reply->error() == QNetworkReply::NoError) {
        return false;
    }
    const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!status.isValid()) {
        return true;  // no HTTP status → a network-layer error (timeout, DNS, reset)
    }
    const int code = status.toInt();
    return (retry404 && code == 404) || code == 429 || code >= 500;
}

// The exponential-backoff delay (ms) before the next attempt of a bounded retry
// ladder: baseMs · 2^attempt, where `attempt` counts up from 0 as `retriesLeft`
// counts down from `maxRetries` (so the first retry waits baseMs, then 2·, 4·…).
// Shared by the search and price transports so they can't drift on backoff policy.
inline int backoffDelayMs(int retriesLeft, int maxRetries, int baseMs) {
    const int attempt = maxRetries - retriesLeft;
    return baseMs * (1 << attempt);
}

// A human-readable one-liner naming the HTTP status (and what it usually means for
// this API), so the logs distinguish rate-limiting from the API just flaking.
inline QString httpStatusNote(QNetworkReply* reply) {
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

}  // namespace pokedex
