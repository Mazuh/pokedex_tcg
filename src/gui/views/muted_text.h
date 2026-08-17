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
// and covers the three foreground roles widgets actually draw text with —
// WindowText (labels), Text (line edits) and ButtonText (buttons) — each sourced
// from its own Disabled entry, so a QLabel, a read-only QLineEdit and a flat
// QToolButton all grey to the same tone. ButtonText is why CardCopyForm's "⚠"
// markers can read as a hint rather than an alarm: a button ignores WindowText.
//
// Header-only alongside the other GUI view helpers; shared by the About box's
// muted lines, CardCopyForm's locked printed-identity fields and its "couldn't be
// autofilled" markers. To undo it, reset the widget to a default palette
// (`w->setPalette(QPalette{})`).
inline void applyMutedText(QWidget* widget) {
    QPalette pal = widget->palette();
    const QColor mutedWindow = pal.color(QPalette::Disabled, QPalette::WindowText);
    const QColor mutedText = pal.color(QPalette::Disabled, QPalette::Text);
    const QColor mutedButton = pal.color(QPalette::Disabled, QPalette::ButtonText);
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) {
        pal.setColor(group, QPalette::WindowText, mutedWindow);
        pal.setColor(group, QPalette::Text, mutedText);
        pal.setColor(group, QPalette::ButtonText, mutedButton);
    }
    widget->setPalette(pal);
}

}  // namespace pokedex
