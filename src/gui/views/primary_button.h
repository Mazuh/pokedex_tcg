#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QEvent>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStringLiteral>
#include <QStyle>

namespace pokedex {

namespace detail {

// GUI — the worker behind applyPrimaryButtonStyle (below): it snapshots the palette's
// accent colours into a stylesheet AND re-applies that stylesheet whenever the button's
// palette changes, so the baked-in colours track a runtime light/dark or system-accent
// switch. (A Qt style sheet can't reference palette roles dynamically — the concrete
// colours have to be re-snapshotted on QEvent::PaletteChange, unlike a plain palette
// which repaints itself.) Parented to the button, so it lives and dies with it.
class PrimaryButtonStyler : public QObject {
public:
    PrimaryButtonStyler(QAbstractButton* button, bool withIcon)
        : QObject(button), button_(button), withIcon_(withIcon) {
        button_->installEventFilter(this);
        apply();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == button_ && !applying_ &&
            (event->type() == QEvent::PaletteChange ||
             event->type() == QEvent::ApplicationPaletteChange)) {
            apply();  // the theme/accent changed under us — restyle from the live palette
        }
        return QObject::eventFilter(watched, event);
    }

private:
    void apply() {
        // setStyleSheet re-polishes the widget, which can itself post a PaletteChange;
        // guard against re-entering apply() from our own restyle.
        applying_ = true;
        const QPalette pal = button_->palette();
        const QColor accent = pal.color(QPalette::Highlight);
        const QColor onAccent = pal.color(QPalette::HighlightedText);
        const QColor hover = accent.lighter(112);
        const QColor pressed = accent.darker(112);
        const QColor disabledBg = pal.color(QPalette::Disabled, QPalette::Button);
        const QColor disabledFg = pal.color(QPalette::Disabled, QPalette::ButtonText);
        // A 2px border is reserved (transparent) in every state so the :focus ring can
        // colour it without shifting the button's size — a stylesheet button loses the
        // native focus ring (there is no chrome left to draw it), so we draw our own for
        // keyboard users.
        button_->setStyleSheet(
            QStringLiteral("QAbstractButton {"
                           "  background-color: %1; color: %2;"
                           "  border: 2px solid transparent; border-radius: 5px;"
                           "  padding: 5px 14px; font-weight: bold;"
                           "}"
                           "QAbstractButton:hover { background-color: %3; }"
                           "QAbstractButton:pressed { background-color: %4; }"
                           "QAbstractButton:focus { border-color: %2; }"
                           "QAbstractButton:disabled { background-color: %5; color: %6;"
                           " border-color: transparent; }")
                .arg(accent.name(), onAccent.name(), hover.name(), pressed.name(),
                     disabledBg.name(), disabledFg.name()));
        if (withIcon_ && button_->icon().isNull()) {
            button_->setIcon(button_->style()->standardIcon(QStyle::SP_DialogApplyButton));
        }
        applying_ = false;
    }

    QAbstractButton* button_;
    bool withIcon_;
    bool applying_ = false;
};

}  // namespace detail

// GUI — give a form's PRIMARY / submit button (Add, Save, Create…) a consistent,
// high-visibility affordance so the user can spot "the button that commits this form"
// at a glance instead of scanning a row of look-alike grey buttons: the OS accent
// colour (the palette's Highlight, so it tracks the system accent and the light/dark
// theme — even on a runtime switch, see PrimaryButtonStyler) plus a confirm "✓" icon.
// Every in-window CRUD form routes its submit button through here — see the "primary
// form button" convention in CLAUDE.md — so the affordance can never drift between the
// Add-copy, Edit-copy, New/Edit-binder, and wishlist forms.
//
// It is applied as a stylesheet rather than via the palette because a native macOS
// QPushButton ignores a palette background colour; a stylesheet paints reliably on
// both the macOS build and the Linux CI build. All button states are spelled out
// (normal/hover/pressed/focus/disabled) so the styled button never falls back to a
// flat, chrome-less look — including its keyboard focus ring.
//
// `withIcon` defaults to true and adds a standard confirm check (only when the button
// has no icon of its own). Pass false for a QToolButton in the default icon-only style,
// where an icon would replace the text label ("Add" → a bare check).
inline void applyPrimaryButtonStyle(QAbstractButton* button, bool withIcon = true) {
    // Parented to the button, so it needs no external owner and is destroyed with it.
    new detail::PrimaryButtonStyler(button, withIcon);
}

}  // namespace pokedex
