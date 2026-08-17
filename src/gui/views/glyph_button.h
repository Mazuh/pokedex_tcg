#pragma once

#include <QObject>
#include <QPoint>
#include <QString>
#include <QToolButton>
#include <QToolTip>
#include <QWidget>

namespace pokedex {

// GUI — the app's "small glyph that reveals an explanation" affordance, in one place.
// A flat tool button showing a single character whose rich-text explanation is BOTH its
// tooltip (hover) and what it pops on click (via QToolTip::showText), so it works with
// either mouse habit; the WhatsThis cursor signals "this reveals help", and NoFocus keeps
// it out of the form's tab order (it explains a field, it isn't one).
//
// The click reads whatever tooltip the button CURRENTLY holds, so a caller may re-word it
// per render (the price surfaces fold the freshness dates into theirs) without re-wiring.
//
// Two glyphs use it today: "ⓘ" (what these options/figures mean — card_copy_form's
// attribute pickers and both price surfaces via price_headline.h's makeInfoButton) and
// "⚠" (card_copy_form's "the catalog couldn't fill this in" markers). Defined once here
// so a third can't drift from them.
inline QToolButton* makeGlyphButton(QWidget* parent, const QString& glyph, const QString& html,
                                    const QString& accessibleName) {
    auto* button = new QToolButton(parent);
    button->setText(glyph);
    button->setAutoRaise(true);
    button->setFocusPolicy(Qt::NoFocus);
    button->setCursor(Qt::WhatsThisCursor);
    button->setToolTip(html);
    button->setAccessibleName(accessibleName);
    QObject::connect(button, &QToolButton::clicked, button, [button]() {
        QToolTip::showText(button->mapToGlobal(QPoint(0, button->height())), button->toolTip(),
                           button);
    });
    return button;
}

}  // namespace pokedex
