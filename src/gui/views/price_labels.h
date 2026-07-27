#pragma once

#include <QString>
#include <QStringList>

#include <map>
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

// The best available price for one vendor, trying `metrics` in preference order and,
// within the first metric that has any row, taking the highest value (the notable
// figure when a card has several finishes). Returns nullptr when the vendor has none
// of the listed metrics — so the headline degrades to whatever the card actually
// carries instead of vanishing when the preferred metric happens to be absent.
inline const CardPrice* bestPrice(const std::vector<CardPrice>& prices, const char* provenance,
                                  std::initializer_list<const char*> metrics) {
    for (const char* metric : metrics) {
        const CardPrice* best = nullptr;
        for (const CardPrice& p : prices) {
            if (p.provenance == provenance && p.metric == metric &&
                (best == nullptr || p.amountCents > best->amountCents)) {
                best = &p;
            }
        }
        if (best != nullptr) {
            return best;
        }
    }
    return nullptr;
}

// The representative TCGplayer + Cardmarket figure for one card (either may be null when
// that vendor carries none of its metrics), using the app-wide metric preference:
// TCGplayer market → mid → low, Cardmarket trendPrice → averageSell → low. The SINGLE
// source both the per-card headline and the binder value total draw from, so the two can
// never pick a different figure — extracting it here keeps that promise structurally
// rather than by two copies of the same literal lists staying in sync by hand.
struct VendorBest {
    const CardPrice* tcg;
    const CardPrice* cm;
};
inline VendorBest vendorBest(const std::vector<CardPrice>& prices) {
    return {bestPrice(prices, kTcgplayerProvenance, {"market", "mid", "low"}),
            bestPrice(prices, kCardmarketProvenance,
                      {"trendPrice", "averageSellPrice", "lowPrice"})};
}

// The per-card figures with NO vendor labels — just the amounts, each with its currency
// symbol ("$800.43 · €1531.00"). The symbol is the only "context" (it says which
// marketplace: $ = TCGplayer/USD, € = Cardmarket/EUR), so the string fits a narrow table
// cell. Empty when the card has no usable figure. Same per-vendor pick as priceHeadline.
inline QString priceAmountsInline(const std::vector<CardPrice>& prices) {
    const VendorBest best = vendorBest(prices);
    QStringList parts;
    if (best.tcg != nullptr) {
        parts << formatMoney(best.tcg->amountCents, best.tcg->currency);
    }
    if (best.cm != nullptr) {
        parts << formatMoney(best.cm->amountCents, best.cm->currency);
    }
    return parts.join(QStringLiteral(" · "));
}

// A compact one-line summary of the spread: one representative figure per vendor —
// e.g. "TCGplayer $800.43 · Cardmarket €1531.00". Empty only when a card truly has no
// usable price, so callers can hide the hint. The full list is the panel's expandable
// table.
inline QString priceHeadline(const std::vector<CardPrice>& prices) {
    const VendorBest best = vendorBest(prices);
    QStringList parts;
    if (best.tcg != nullptr) {
        parts << QStringLiteral("TCGplayer ") +
                     formatMoney(best.tcg->amountCents, best.tcg->currency);
    }
    if (best.cm != nullptr) {
        parts << QStringLiteral("Cardmarket ") +
                     formatMoney(best.cm->amountCents, best.cm->currency);
    }
    return parts.join(QStringLiteral(" · "));
}

// Add one card's representative per-vendor figures into running per-currency totals
// (keyed by ISO currency code), via the SAME vendorBest pick as priceHeadline. Currencies
// stay separate — no FX rate is invented (USD from TCGplayer, EUR from Cardmarket
// accumulate independently). A card with no usable price adds nothing. Used to total a
// binder's value across many copies; note this is the headline's "notable figure" per
// vendor (the highest across a card's finishes), so a summed total is a rough estimate of
// worth, not a precise per-finish valuation — like the headline it can't tell which finish
// a given copy is.
inline void accumulateBestPrices(std::map<std::string, long long>& totalsByCurrency,
                                 const std::vector<CardPrice>& prices) {
    const VendorBest best = vendorBest(prices);
    if (best.tcg != nullptr) {
        totalsByCurrency[best.tcg->currency] += best.tcg->amountCents;
    }
    if (best.cm != nullptr) {
        totalsByCurrency[best.cm->currency] += best.cm->amountCents;
    }
}

// Per-currency cent totals → a compact string like "$120.50 · €35.00" (empty when
// there are no totals). USD is emitted first to match the finder/panel's
// TCGplayer-then-Cardmarket ordering; any other currencies follow in code order.
inline QString formatMoneyTotals(const std::map<std::string, long long>& totalsByCurrency) {
    QStringList parts;
    if (const auto it = totalsByCurrency.find("USD"); it != totalsByCurrency.end()) {
        parts << formatMoney(it->second, "USD");
    }
    for (const auto& [currency, cents] : totalsByCurrency) {
        if (currency != "USD") {
            parts << formatMoney(cents, currency);
        }
    }
    return parts.join(QStringLiteral(" · "));
}

}  // namespace pokedex
