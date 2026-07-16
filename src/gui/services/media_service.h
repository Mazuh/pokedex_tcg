#pragma once

#include <QHash>
#include <QObject>
#include <QPixmap>
#include <QString>

#include "core/app/pokemon_external_api.h"

class QNetworkAccessManager;
class QNetworkReply;

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
    void startFetch(int dexNumber, MediaKind kind, const QString& url,
                    const QString& relPath, const QString& cachePath);

    const PokemonExternalApi& api_;
    QString mediaDir_;
    QNetworkAccessManager* nam_;
    // In-flight GETs keyed by cache-relative path, so a concurrent request for the
    // same target reuses the reply instead of firing a second fetch.
    QHash<QString, QNetworkReply*> inFlight_;
};

}  // namespace pokedex
