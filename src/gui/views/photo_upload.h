#pragma once

#include <QFileDialog>
#include <QMessageBox>
#include <QObject>
#include <QPixmap>
#include <QString>
#include <QWidget>

#include <optional>

namespace pokedex {

// GUI — the shared "pick a local card photo" step used by both card-copy upload
// flows (Add-copy holds the result in memory until submit; Edit-card writes it to
// the store immediately). Opens the file dialog, and returns the decoded pixmap on
// success. Returns nullopt when the user cancels, or — after warning them — when the
// chosen file can't be read as an image; the caller does nothing either way. Kept
// here so the dialog's format filter and the error wording live in one place rather
// than being duplicated across the two pages.
inline std::optional<QPixmap> pickCardPhoto(QWidget* parent) {
    const QString path = QFileDialog::getOpenFileName(
        parent, QObject::tr("Choose a card photo"), QString(),
        QObject::tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (path.isEmpty()) {
        return std::nullopt;  // cancelled
    }
    QPixmap pixmap(path);
    if (pixmap.isNull()) {
        QMessageBox::warning(
            parent, QObject::tr("Pokedex TCG"),
            QObject::tr("That file could not be read as an image. Try a PNG or JPEG."));
        return std::nullopt;
    }
    return pixmap;
}

}  // namespace pokedex
