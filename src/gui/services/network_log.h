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
// issues `request` on `nam` via GET, but first logs the outbound URL and bumps a
// per-host session counter (also logged) — so there is exactly one place that
// sees, and tallies, every call we make to a free public API. Returns the reply
// exactly as `nam->get(request)` would, so callers wire up `finished` unchanged.
//
// GUI-thread only (like the QNetworkAccessManager it wraps): the per-host tally is
// a plain static map with no locking, matching how these services are used.
QNetworkReply* loggedGet(QNetworkAccessManager* nam, const QNetworkRequest& request);

}  // namespace pokedex
