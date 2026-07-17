#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include <string>

class QNetworkAccessManager;
class QNetworkReply;
class QPixmap;

namespace pokedex {

// GUI — the on-disk store for owned-card images, rooted at the workspace media
// dir. Unlike MediaService (Pokémon artwork, fetched + cached from an external
// API) this holds the one image a committed CardCopy keeps — the printing the user
// picked while adding the copy — saved at creation time and loaded back when the
// copy is shown in "My Cards".
//
// A copy's image is keyed by the copy's synthetic id (see cardImageCacheRelPath),
// which is filesystem-safe and available both when saving and when displaying. The
// stable path (<mediaDir>/cards/<copyId>.png) also lets a user later drop in their
// own photo by hand — e.g. for a card too new for the card API.
//
// Two ways in. save() persists a pixmap already in hand (the add-copy preview) —
// synchronous, no network. fetchAndSave() downloads the image from a URL first —
// used when the copy is submitted before its preview finished loading, so a picked
// card still gets its art. It is a QObject only for that async path; because one
// instance is owned by main() (outliving every view), a download kicked off as the
// add-copy page closes still completes and lands on disk. load() is a synchronous
// local read, like MediaService's cache-hit path.
class CardImageStore : public QObject {
    Q_OBJECT

public:
    // `mediaDir` is the workspace media directory (Workspace::mediaDir()); card
    // images land under its cards/ subfolder.
    explicit CardImageStore(QString mediaDir, QObject* parent = nullptr);

    // The absolute path a copy's image is stored at (whether or not it exists).
    QString pathFor(const std::string& copyId) const;

    // Persist `pixmap` as this copy's image, creating cards/ as needed and writing
    // atomically (temp-write-then-rename) so a crash mid-write leaves no partial
    // file. Returns false (and leaves any prior file intact) on a null pixmap or a
    // write/encode failure; callers treat the image as best-effort.
    bool save(const std::string& copyId, const QPixmap& pixmap) const;

    // Download the image at `url` and persist it as this copy's image (same atomic
    // write as save()). Best-effort and fire-and-forget: a blank url, a failed
    // fetch, or invalid image bytes simply leaves no file (logged, never thrown).
    // Concurrent fetches for the same copy are de-duplicated.
    void fetchAndSave(const std::string& copyId, const QString& url);

    // Load a copy's image from disk, or a null QPixmap when none is stored (or the
    // file is unreadable/undecodable — e.g. a hand-placed file that isn't an image).
    QPixmap load(const std::string& copyId) const;

private:
    QString mediaDir_;
    QNetworkAccessManager* nam_ = nullptr;  // created lazily on the first fetch
    // In-flight downloads keyed by cache path, so a repeat fetch for the same copy
    // reuses the reply instead of firing a second GET.
    QHash<QString, QNetworkReply*> inFlight_;
};

}  // namespace pokedex
