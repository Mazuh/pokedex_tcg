#pragma once

#include <QLabel>
#include <QString>

namespace pokedex {

// GUI — is this wishlist source a clickable link (a marketplace URL) rather than
// a plain seller name? Sources are free text; by convention a link is an
// "http://" or "https://" URL. Require the full scheme, not a bare "http" prefix,
// so a seller name like "HTTP Trading Post" stays plain text rather than becoming
// a broken link. Kept header-only alongside the other GUI helpers so both the
// per-Pokémon editor and the unscoped wishlist table render sources the same way.
inline bool isLinkSource(const QString& source) {
    return source.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
           source.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive);
}

// GUI — a label rendering a wishlist source: a URL becomes a clickable link that
// opens in the default browser (openExternalLinks); a seller name stays plain,
// selectable text. The text is HTML-escaped so a stray '<' in a source can never
// be misread as markup.
inline QLabel* sourceLabel(const QString& source, QWidget* parent = nullptr) {
    auto* label = new QLabel(parent);
    const QString safe = source.toHtmlEscaped();
    if (isLinkSource(source)) {
        label->setText(QStringLiteral("<a href=\"%1\">%1</a>").arg(safe));
        label->setOpenExternalLinks(true);
        label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    } else {
        label->setText(safe);
        label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    }
    label->setWordWrap(true);
    return label;
}

}  // namespace pokedex
