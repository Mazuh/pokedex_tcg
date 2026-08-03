#pragma once

#include <QLoggingCategory>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

namespace pokedex {

// The logging category for every outbound external-API call. Enabled at info
// level by default so calls are visible on stderr without extra setup; silence
// with e.g. QT_LOGGING_RULES="pokedex.net.info=false", or raise other categories
// without touching this one.
Q_DECLARE_LOGGING_CATEGORY(lcNet)

// The single chokepoint every GUI service routes its external GETs through. It
// logs the outbound URL and bumps a per-host session counter (also logged) — so
// there is exactly one place that sees, and tallies, every call we make to a free
// public API — then issues `request` on `nam` via GET, returning the reply exactly
// as `nam->get(request)` would (callers wire up `finished` unchanged). It also
// *owns* the safe-redirect policy (`NoLessSafeRedirectPolicy`), applying it to
// every request here so no call site can forget it — hence `request` is taken by
// value. Callers pass a bare `QNetworkRequest{url}` (plus any other attributes/
// headers they need).
//
// GUI-thread only (like the QNetworkAccessManager it wraps): the per-host tally is
// a plain static map with no locking, matching how these services are used.
QNetworkReply* loggedGet(QNetworkAccessManager* nam, QNetworkRequest request);

// The POST counterpart, for the outbound calls that send a body (the AI assistant).
// Same chokepoint contract: it logs the URL and bumps the shared per-host tally,
// then issues the POST with `body`. It logs ONLY the URL — never the request
// headers or body — so a secret carried in a header (an API key) is never written
// to the log. Callers set their own content-type / auth headers on `request`
// before calling; the safe-redirect policy is applied here as in loggedGet.
QNetworkReply* loggedPost(QNetworkAccessManager* nam, QNetworkRequest request,
                          const QByteArray& body);

}  // namespace pokedex
