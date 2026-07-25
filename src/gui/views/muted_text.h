#pragma once

#include <QColor>
#include <QPalette>
#include <QWidget>

namespace pokedex {

// GUI — grey a widget's text to the theme's *disabled* colour WITHOUT disabling
// the widget, so it reads as secondary / "locked" while staying interactive
// (selectable text, working links, copyable line edits). Disabling the widget
// would instead make it swallow mouse input. Recolours both the Active and
// Inactive groups (so the muted look holds whether or not the window is focused)
// and covers the two foreground roles widgets actually draw text with —
// WindowText (labels) and Text (line edits) — each sourced from its own Disabled
// entry, so a QLabel and a read-only QLineEdit both grey to the same tone.
//
// Header-only alongside the other GUI view helpers; shared by the About box's
// muted lines and CardCopyForm's locked printed-identity fields. To undo it,
// reset the widget to a default palette (`w->setPalette(QPalette{})`).
inline void applyMutedText(QWidget* widget) {
    QPalette pal = widget->palette();
    const QColor mutedWindow = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor mutedText = pal.color(QPalette::Disabled, QPalette::Text);
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        pal.setColor(group, QPalette::WindowText, mutedWindow);
        pal.setColor(group, QPalette::Text, mutedText);
    }
    widget->setPalette(pal);
}

}  // namespace pokedex
