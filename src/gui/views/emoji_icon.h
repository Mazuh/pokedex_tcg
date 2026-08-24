#pragma once

#include <QFont>
#include <QGuiApplication>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QSize>
#include <QString>

namespace pokedex {

// GUI — paints one emoji (or any other character the system font can draw) into a pixmap
// and hands it back as a QIcon, so a glyph can decorate a widget that takes an icon rather
// than being prefixed onto its text. Rendered rather than shipped as an asset: an emoji is
// the one "image" the system font already has at every size, needing no resource entry, no
// install path, and no platform-specific downscale step.
//
// The distinction it exists for is not cosmetic — a QComboBox's and a QListWidget's
// type-ahead both match keystrokes against the DISPLAYED TEXT, so a glyph baked into a
// label makes the entry unreachable by keyboard. As an icon the glyph sits beside the text
// without joining what that text is searched by, and the icons line up in a column that a
// variable-width prefix wouldn't.
//
// `box` is the pixmap the icon occupies; `pixelSize` is how big the glyph is drawn inside
// it. They are SEPARATE parameters rather than one, because a box that merely equals the
// pixel size silently CROPS the glyph: a colour emoji's ink measures about 1.2x its pixel
// size, and drawText centres on the font's line box (ascent/descent), not on the ink, so
// the ink can sit off-centre and meet an edge even when it would nominally fit. Measured
// against this Qt on macOS: at pixelSize 18 the ink is 21.8 x 21.2, so an 18x18 box clips
// every glyph on every side and a 24x24 box clips none. Budget roughly **1.33x the pixel
// size in each dimension you don't want cropped**, and re-measure rather than reason: paint
// the glyph and check whether any pixel with alpha lands on a border row or column.
//
// The two dimensions are independent, which is why the size can't just be derived: a
// language flag's ink is wide and short, so it fits the pickers' 22x16 box at pixelSize 16,
// while a square emoji at that size would not. (The one square glyph in that list, the
// Americas globe standing in for "LA", is consequently cropped top and bottom — long
// predating this helper, and left alone because widening the box would grow every row of
// every language picker.)
//
// Two more things are load-bearing. The pixmap is allocated at the device pixel ratio and
// then tagged with it, so the painting happens in logical coordinates and the glyph stays
// sharp on a retina display. And the result is deliberately NOT cached in a static: a
// QPixmap outliving QGuiApplication is a documented crash-at-exit, and repainting a handful
// of glyphs when a page opens is far too cheap to be worth that hazard.
//
// Callers must also give the widget a matching setIconSize(box): a QComboBox defaults to a
// 16x16 box and an item view to an invalid one (falling back to the style's small-icon
// size, also 16 here), so leaving it scales the icon down to a fraction of the row's
// height.
inline QIcon emojiIcon(const QString& glyph, QSize box, int pixelSize) {
    if (glyph.isEmpty()) return {};

    QPixmap pixmap(box * qApp->devicePixelRatio());
    pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        QFont font = QGuiApplication::font();
        font.setPixelSize(pixelSize);
        painter.setFont(font);
        painter.drawText(QRect(0, 0, box.width(), box.height()), Qt::AlignCenter, glyph);
    }
    return QIcon(pixmap);
}

}  // namespace pokedex
