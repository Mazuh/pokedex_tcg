#pragma once

#include <QLabel>
#include <QPixmap>

namespace pokedex {

// GUI — set `src` on `label`, scaled to fit while keeping aspect ratio and staying
// crisp on high-DPI displays. Every image panel that shows fetched art (Pokémon
// artwork, the add-copy preview, the My Cards card image) scales the same way, so
// the device-pixel-ratio dance lives here once rather than being copy-pasted into
// each renderImage(). Callers still own the null/placeholder branch — this is only
// the "there is a pixmap, draw it" path.
inline void setScaledPixmap(QLabel* label, const QPixmap& src) {
    const qreal dpr = label->devicePixelRatioF();
    QPixmap scaled =
        src.scaled(label->size() * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    label->setPixmap(scaled);
}

}  // namespace pokedex
