#include "gui/services/media_service.h"

#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>

#include <utility>

#include "core/app/media_cache_layout.h"

namespace pokedex {

MediaService::MediaService(const PokemonExternalApi& api, QString mediaDir, QObject* parent)
    : QObject(parent),
      api_(api),
      mediaDir_(std::move(mediaDir)),
      nam_(new QNetworkAccessManager(this)) {}

void MediaService::request(const MediaSubject& subject, MediaKind kind) {
    const int dex = subject.dexNumber;
    const MediaRequest resolved = api_.resolveMedia(subject, kind);
    if (resolved.resourceName.empty() || resolved.url.empty()) {
        qWarning() << "MediaService: external API cannot serve dex" << dex;
        Q_EMIT failed(dex, kind);
        return;
    }

    const QString relPath = QString::fromStdString(mediaCacheRelPath(resolved.resourceName, kind));
    const QString cachePath = QDir(mediaDir_).filePath(relPath);

    // Cache hit: load from disk, no network. A file that fails to decode
    // (truncated by an old crash) is treated as a miss and re-fetched.
    if (QFileInfo::exists(cachePath)) {
        QPixmap pixmap;
        if (pixmap.load(cachePath)) {
            // Deliver asynchronously so callers always receive ready() *after*
            // returning from request() — a uniform contract with the network path.
            QMetaObject::invokeMethod(
                this, [this, dex, kind, pixmap]() { Q_EMIT ready(dex, kind, pixmap); },
                Qt::QueuedConnection);
            return;
        }
        qWarning() << "MediaService: cached file failed to decode, refetching:" << cachePath;
    }

    startFetch(dex, kind, QString::fromStdString(resolved.url), relPath, cachePath);
}

void MediaService::startFetch(int dexNumber, MediaKind kind, const QString& url,
                              const QString& relPath, const QString& cachePath) {
    // De-dup: a concurrent request for the same cache target reuses the in-flight
    // reply rather than firing a second GET. Its finished handler emits ready()
    // for everyone (the panel's stale-guard ignores results it no longer wants).
    if (inFlight_.contains(relPath)) {
        return;
    }

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(request);
    inFlight_.insert(relPath, reply);

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, dexNumber, kind, relPath, cachePath]() {
                inFlight_.remove(relPath);
                reply->deleteLater();

                if (reply->error() != QNetworkReply::NoError) {
                    qWarning() << "MediaService: fetch failed for dex" << dexNumber << ":"
                               << reply->errorString();
                    Q_EMIT failed(dexNumber, kind);
                    return;
                }

                const QByteArray bytes = reply->readAll();
                QPixmap pixmap;
                // Validate before caching: a 404 HTML body or a truncated download
                // must never land on disk as a .png.
                if (bytes.isEmpty() || !pixmap.loadFromData(bytes)) {
                    qWarning() << "MediaService: invalid image bytes for dex" << dexNumber
                               << "(" << bytes.size() << "bytes )";
                    Q_EMIT failed(dexNumber, kind);
                    return;
                }

                // Create the per-species directory, then write atomically
                // (temp-write-then-rename) so a crash mid-write leaves no partial file.
                QDir().mkpath(QFileInfo(cachePath).absolutePath());
                QSaveFile file(cachePath);
                if (!file.open(QIODevice::WriteOnly) ||
                    file.write(bytes) != bytes.size() || !file.commit()) {
                    // The cache write failed, but the image is valid in memory —
                    // deliver it rather than failing the user-visible request.
                    qWarning() << "MediaService: could not cache" << cachePath << ":"
                               << file.errorString();
                }
                Q_EMIT ready(dexNumber, kind, pixmap);
            });
}

}  // namespace pokedex
