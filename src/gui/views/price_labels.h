#pragma once

#include <QString>
#include <QStringList>

#include <string>
#include <vector>

#include "core/app/card_price_dto.h"

namespace pokedex {

// GUI — display helpers turning core CardPrice data into strings. Kept out of the
// Qt-free core (which stores money as integer cents and vendor labels as plain
// strings) so currency symbols and the "headline" presentation live on the GUI side.
// Shared by the finder's subtle price hint and the owned-copy CardPricesPanel, so the
// two never format money or pick a headline differently.

inline QString currencySymbol(const QString& code) {
    if (code == QLatin1String("USD")) {
        return QStringLiteral("$");
    }
    if (code == QLatin1String("EUR")) {
        return QStringLiteral("€");
    }
    if (code == QLatin1String("GBP")) {
        return QStringLiteral("£");
    }
    return code + QLatin1Char(' ');  // unknown currency: show the ISO code as a prefix
}

// Integer minor units + currency → a display amount, e.g. (80043, "USD") → "$800.43".
// Assumes a 2-decimal (cents) minor unit, which holds for every currency the app
// currently produces — tcgplayer quotes USD and cardmarket EUR. A zero-decimal
// currency (e.g. JPY) would misformat; revisit this alongside manual price entry
// (deferred), which is the only path that could introduce another currency.
inline QString formatMoney(long long cents, const std::string& currency) {
    const QString sym = currencySymbol(QString::fromStdString(currency));
    const long long whole = cents / 100;
    const int frac = static_cast<int>(cents % 100);
    return sym + QString::number(whole) + QLatin1Char('.') +
           QStringLiteral("%1").arg(frac, 2, 10, QLatin1Char('0'));
}

// A compact one-line summary of the spread: the TCGplayer "market" price (the
// highest across variants — the notable figure) and the Cardmarket "trend" price,
// e.g. "TCGplayer $800.43 · Cardmarket €1531.00". Empty when neither is present, so
// callers can hide the hint. Deliberately picks two representative numbers rather
// than the whole spread — the full list is the panel's expandable table.
inline QString priceHeadline(const std::vector<CardPrice>& prices) {
    const CardPrice* tcg = nullptr;
    const CardPrice* cm = nullptr;
    for (const CardPrice& p : prices) {
        if (p.provenance == "tcgplayer" && p.metric == "market") {
            if (tcg == nullptr || p.amountCents > tcg->amountCents) {
                tcg = &p;
            }
        } else if (p.provenance == "cardmarket" && p.metric == "trendPrice") {
            cm = &p;
        }
    }
    QStringList parts;
    if (tcg != nullptr) {
        parts << QStringLiteral("TCGplayer ") + formatMoney(tcg->amountCents, tcg->currency);
    }
    if (cm != nullptr) {
        parts << QStringLiteral("Cardmarket ") + formatMoney(cm->amountCents, cm->currency);
    }
    return parts.join(QStringLiteral(" · "));
}

}  // namespace pokedex
