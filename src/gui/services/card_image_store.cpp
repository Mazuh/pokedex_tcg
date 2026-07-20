#include "gui/services/card_image_store.h"

#include <QBuffer>
#include <QByteArray>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSaveFile>
#include <QUrl>

#include <utility>

#include "core/app/media_cache_layout.h"

namespace pokedex {

namespace {

// Write `bytes` to `path` atomically (temp-write-then-rename), creating parent dirs
// — mirrors MediaService's cache write, so a crash mid-write can't leave a
// truncated .png. Returns false (logged) on any failure.
bool writeAtomically(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
        !file.commit()) {
        qWarning() << "CardImageStore: could not write" << path << ":" << file.errorString();
        return false;
    }
    return true;
}

}  // namespace

CardImageStore::CardImageStore(QString mediaDir, QObject* parent)
    : QObject(parent), mediaDir_(std::move(mediaDir)) {}

QString CardImageStore::pathFor(const std::string& copyId) const {
    return QDir(mediaDir_).filePath(QString::fromStdString(cardImageCacheRelPath(copyId)));
}

bool CardImageStore::save(const std::string& copyId, const QPixmap& pixmap) {
    if (pixmap.isNull()) {
        return false;
    }
    const QString path = pathFor(copyId);
    // A user's explicit save supersedes any still-in-flight download for this copy:
    // abort it so its late completion can't overwrite the image just chosen. abort()
    // makes the reply's finished handler run (with an error), which cleans it up.
    if (QNetworkReply* reply = inFlight_.take(path)) {
        reply->abort();
    }
    // Encode to PNG in memory, then commit the bytes atomically.
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !pixmap.save(&buffer, "PNG")) {
        qWarning() << "CardImageStore: could not encode image for copy"
                   << QString::fromStdString(copyId);
        return false;
    }
    if (!writeAtomically(path, bytes)) {
        return false;
    }
    Q_EMIT imageChanged(QString::fromStdString(copyId));
    return true;
}

void CardImageStore::fetchAndSave(const std::string& copyId, const QString& url) {
    if (url.isEmpty()) {
        return;  // nothing to fetch (the printing had no image)
    }
    const QString path = pathFor(copyId);
    if (inFlight_.contains(path)) {
        return;  // a fetch for this copy is already running
    }
    if (nam_ == nullptr) {
        nam_ = new QNetworkAccessManager(this);
    }

    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam_->get(request);
    inFlight_.insert(path, reply);

    connect(reply, &QNetworkReply::finished, this, [this, reply, path, copyId]() {
        inFlight_.remove(path);
        reply->deleteLater();

        if (reply->error() != QNetworkReply::NoError) {
            // Includes the deliberate abort() a save() fires to supersede this fetch.
            qWarning() << "CardImageStore: fetch failed for" << path << ":"
                       << reply->errorString();
            return;
        }
        const QByteArray bytes = reply->readAll();
        // Validate before writing: a 404 HTML body or a truncated download must
        // never land on disk as a .png.
        QPixmap probe;
        if (bytes.isEmpty() || !probe.loadFromData(bytes)) {
            qWarning() << "CardImageStore: invalid image bytes for" << path << "("
                       << bytes.size() << "bytes )";
            return;
        }
        if (writeAtomically(path, bytes)) {
            Q_EMIT imageChanged(QString::fromStdString(copyId));
        }
    });
}

void CardImageStore::remove(const std::string& copyId) {
    const QString path = pathFor(copyId);
    // Abort any in-flight download first, so its late completion can't recreate the
    // file we're about to delete (mirrors save()'s supersede-the-fetch handling).
    if (QNetworkReply* reply = inFlight_.take(path)) {
        reply->abort();
    }
    if (!QFileInfo::exists(path)) {
        return;  // nothing stored — the copy never had an image
    }
    if (!QFile::remove(path)) {
        qWarning() << "CardImageStore: could not delete" << path;
    }
}

QPixmap CardImageStore::load(const std::string& copyId) const {
    const QString path = pathFor(copyId);
    QPixmap pixmap;
    if (!QFileInfo::exists(path) || !pixmap.load(path)) {
        return QPixmap();  // none stored, or a hand-placed file that isn't an image
    }
    return pixmap;
}

}  // namespace pokedex
