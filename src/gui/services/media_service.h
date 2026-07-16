#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>

#include <optional>

#include "core/app/pokemon_external_api.h"
#include "core/app/rate_limiter.h"

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace pokedex {

// GUI — the transport + cache half of the external-API module. A QObject that
// fetches a subject's media (official artwork today) and caches it under the
// workspace media dir, so a second view loads from disk and works offline. It
// depends only on the Qt-free PokemonExternalApi seam for *where* to fetch and
// *what to call the cache file* — it never names PokeAPI itself.
//
// Cache-first and asynchronous: request() resolves the fetch instruction, checks
// the on-disk cache, and either emits ready() (queued, so callers always receive
// it after returning) or fires a single GET whose reply validates, atomically
// caches, decodes, and emits. Every failure path logs and emits failed(); it
// never throws or crashes. Concurrent requests for the same cache target are
// de-duplicated to one in-flight reply.
//
// Cache hits are never gated — they resolve straight off disk. Only the network
// path is throttled, by two cooperating guards so a key-repeat scroll through
// the list does not storm the external API:
//   * A latest-wins coalescing debounce. A cache-missing request is not fired
//     immediately; it is held as the single pending target and fired only after
//     a short quiet interval. Each newer request replaces the pending one (a
//     cache hit, being the target the user landed on, cancels it outright), so
//     scrolled-past species never hit the wire — only the one settled on does.
//   * A token-bucket rate limiter as a hard backstop for whatever slips past the
//     debounce (a sustained stream of distinct settles, or a future non-interactive
//     caller). When it denies, the pending fetch is not dropped or surfaced as an
//     error — it is simply retried once a token refills, so the request paces
//     itself to a safe rate and still lands.
//
// One instance is shared across the views (owned by main(), outliving the
// window), so the disk cache and in-flight table are shared: a species fetched
// in one section is instantly available in another.
class MediaService : public QObject {
    Q_OBJECT

public:
    // `mediaDir` is the workspace media directory (Workspace::mediaDir()); cache
    // files land beneath it. `api` must outlive this service.
    MediaService(const PokemonExternalApi& api, QString mediaDir, QObject* parent = nullptr);

    // Fetch the media asset for `subject` of the given `kind`. Emits ready() with
    // the decoded pixmap on success (from cache or network) or failed() on any
    // error — always asynchronously.
    void request(const MediaSubject& subject, MediaKind kind);

Q_SIGNALS:
    void ready(int dexNumber, MediaKind kind, const QPixmap& pixmap);
    void failed(int dexNumber, MediaKind kind);

private:
    // A resolved, cache-missing fetch waiting out the debounce interval. Only the
    // most recent one is ever held (latest-wins coalescing).
    struct PendingFetch {
        int dexNumber;
        MediaKind kind;
        QString url;
        QString relPath;
        QString cachePath;
    };

    // Fired when the debounce timer elapses: consult the rate limiter and either
    // start the pending GET or, if throttled, reschedule to retry once a token
    // refills.
    void dispatchPending();
    void startFetch(int dexNumber, MediaKind kind, const QString& url,
                    const QString& relPath, const QString& cachePath);

    const PokemonExternalApi& api_;
    QString mediaDir_;
    QNetworkAccessManager* nam_;
    // In-flight GETs keyed by cache-relative path, so a concurrent request for the
    // same target reuses the reply instead of firing a second fetch.
    QHash<QString, QNetworkReply*> inFlight_;
    // The network-path throttle: a debounce timer holding the single pending
    // target, plus a token bucket bounding the sustained fetch rate.
    QTimer* debounceTimer_;
    std::optional<PendingFetch> pending_;
    TokenBucket limiter_;
};

}  // namespace pokedex
