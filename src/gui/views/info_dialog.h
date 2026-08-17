#pragma once

#include <QDialog>
#include <QString>

class QTextBrowser;

namespace pokedex {

// GUI — the modal explainer behind every "ⓘ" affordance (the card form's Condition /
// Rarity / Foil pickers and both price surfaces; see info_button.h, which is the only
// thing that opens it). A deliberate modal, one of the few sanctioned exceptions to the
// "prefer a pushed page over a dialog" rule (like AboutDialog and FirstRunDialog): it is
// a read-and-dismiss aside from a half-filled form, so a pushed page would cost the user
// their place in what they were typing.
//
// It replaced QToolTip::showText, which could not carry these texts: a tooltip does not
// scroll and Qt clamps it to the screen, so a long explanation — the rarity list is 17
// definition entries — was simply unreadable on a laptop display (the popup auto-closes
// rather than being cut off). Hence the body is a QTextBrowser: it scrolls, and (unlike a
// word-wrapped QLabel, whose size policy carries no heightForWidth flag, so a QScrollArea
// would clip it with no scrollbar) it reports an exact laid-out height. The dialog opens
// snug around a short explanation and caps at a share of the screen for a long one.
class InfoDialog : public QDialog {
    Q_OBJECT

public:
    InfoDialog(const QString& title, const QString& bodyHtml, QWidget* parent = nullptr);

private:
    QTextBrowser* body_;
};

}  // namespace pokedex
