#pragma once

#include <QString>
#include <QTextDocument>  // Qt::mightBeRichText
#include <QtGlobal>

namespace pokedex {

// GUI — make an arbitrary string safe to hand to setToolTip. Qt tooltips are
// Qt::AutoText: Qt::mightBeRichText() decides per string whether it is HTML, so a
// value that happens to look like markup ("Shop <b>deals</b>" as a binder name or a
// wishlist source) would be RENDERED as markup and the user's own characters would
// silently disappear. Since the tables now tooltip free text everywhere (see
// table_cell.h), that decision has to be made once, here.
//
// The rule is conditional, and deliberately not "always escape": Qt does NOT treat an
// escaped ampersand as rich text, so blanket-escaping would print a literal "&amp;" in
// every "Scarlet & Violet" set name and every URL with a query string — a regression on
// the common case to defend against the rare one. So text that reads as plain is passed
// through untouched, and only text Qt would treat as markup is escaped — which keeps it
// on the rich-text path (an escaped tag still looks like markup to Qt), where the
// entities decode back to exactly what was typed. Newlines become <br> in that branch
// for the same reason: rich text collapses them.
inline QString tooltipText(const QString& text) {
    if (!Qt::mightBeRichText(text)) {
        return text;
    }
    QString escaped = text.toHtmlEscaped();
    escaped.replace(QLatin1Char('\n'), QLatin1String("<br>"));
    return escaped;
}

}  // namespace pokedex
