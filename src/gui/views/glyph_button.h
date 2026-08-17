#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QToolButton>
#include <QToolTip>
#include <QWidget>

namespace pokedex {

// GUI — the app's "small glyph beside a field that reveals an explanation" affordance: a
// flat tool button showing a single character, kept out of the form's tab order (it
// explains a field, it isn't one). The LOOK lives here; the two behaviors built on it are
// deliberately separate, because how much text they carry differs by an order of magnitude:
//
//   • makeHintButton (below) — the "⚠" markers: a SHORT hint, shown as the tooltip on hover
//     and popped by QToolTip::showText on click, so it works with either mouse habit.
//   • makeInfoButton (info_button.h) — the "ⓘ" explainers: a long rich-text reference
//     (the rarity list is 17 definition entries), which opens the modal InfoDialog. A
//     tooltip cannot hold one — it does not scroll and Qt clamps it to the screen, so it
//     auto-closed unread on a laptop display. That is why the ⓘ moved off this idiom.
//
// Use makeHintButton only while the text stays a sentence or two; the moment it grows into
// a reference, it belongs in the dialog.
inline QToolButton* makeGlyphButton(QWidget* parent, const QString& glyph,
                                    const QString& accessibleName) {
    auto* button = new QToolButton(parent);
    button->setText(glyph);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAccessibleName(accessibleName);
    return button;
}

// The tooltip-carrying glyph (the "⚠" markers). The click reads whatever tooltip the button
// CURRENTLY holds, so a caller may re-word it per render without re-wiring.
inline QToolButton* makeHintButton(QWidget* parent, const QString& glyph, const QString& html,
                                   const QString& accessibleName) {
    QToolButton* button = makeGlyphButton(parent, glyph, accessibleName);
    button->setCursor(Qt::WhatsThisCursor);  // "this reveals help"
    button->setToolTip(html);
    QObject::connect(button, &QToolButton::clicked, button, [button]() {
        QToolTip::showText(button->mapToGlobal(QPoint(0, button->height())), button->toolTip(),
                           button);
    });
    return button;
}

}  // namespace pokedex
