#pragma once

#include <QColor>
#include <QPalette>
#include <QWidget>

namespace pokedex {

// GUI — paint a widget's text amber, the "look at this" counterpart to
// applyMutedText. Used by CardCopyForm's "⚠ not filled in for you" markers: they
// are a hint, not an error (red would claim the record is invalid, and the field
// really is optional), but the disabled-grey they used to wear made them so quiet
// that the one thing they exist for — catching the eye on a field the catalog left
// empty — did not happen.
//
// Two tones rather than one, picked off the window background: a saturated yellow
// is illegible on a light form, and a dark goldenrod disappears into a dark one.
// Like applyMutedText this covers WindowText (labels), Text (line edits) and
// ButtonText (buttons) across the Active and Inactive groups — ButtonText is the
// load-bearing one here, since the markers are flat QToolButtons and a button
// ignores WindowText. To undo it, reset the widget (`w->setPalette(QPalette{})`).
//
// An explicit palette colour does NOT follow a later theme switch, so a host that
// outlives one re-applies this from its changeEvent on QEvent::PaletteChange —
// the palette-side equivalent of what PrimaryButtonStyler does for its stylesheet.
inline QColor warningTextColor(const QPalette& palette) {
    const bool darkBackground = palette.color(QPalette::Window).lightness() < 128;
    return darkBackground ? QColor(0xF5, 0xC2, 0x42) : QColor(0xA8, 0x5C, 0x00);
}

inline void applyWarningText(QWidget* widget) {
    QPalette pal = widget->palette();
    const QColor amber = warningTextColor(pal);
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        pal.setColor(group, QPalette::WindowText, amber);
        pal.setColor(group, QPalette::Text, amber);
        pal.setColor(group, QPalette::ButtonText, amber);
    }
    widget->setPalette(pal);
}

}  // namespace pokedex
