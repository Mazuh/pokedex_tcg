#pragma once

#include <QString>
#include <QStringList>

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_foil.h"

namespace pokedex {

// GUI — the tcgdex pricing FINISH a copy's CardFoil maps to. tcgdex prices only three finishes
// (normal/holo/reverse); the finer treatments (Cosmos, Mirror, Cracked Ice, …) all price as
// "holo". An unset foil yields "" — no finish preference, so the pick falls back to the highest
// figure (the prior behavior), which never under-prices a card whose finish we don't know.
inline std::string finishForFoil(std::optional<CardFoil> foil) {
    if (!foil) {
        return "";
    }
    switch (*foil) {
        case CardFoil::NonHolo:
            return "normal";
        case CardFoil::ReverseHolo:
            return "reverse";
        case CardFoil::Holo:
        case CardFoil::CosmosHolo:
        case CardFoil::MirrorHolo:
        case CardFoil::CrackedIceHolo:
        case CardFoil::ConfettiHolo:
        case CardFoil::CrosshatchHolo:
        case CardFoil::HDHolo:
        case CardFoil::Textured:
            return "holo";
    }
    return "";  // unreachable — the switch is exhaustive (a new CardFoil fails -Wswitch)
}

// GUI — drop every price row whose vendor the user has suppressed for this card, so a hidden
// vendor never reaches the headline, the table Prices column, or a binder value total. The
// suppressed list holds provenance tokens (kTcgplayerProvenance / kCardmarketProvenance);
// manual rows are never suppressed. Returns the kept rows (the input unchanged when nothing is
// suppressed, the common case).
inline std::vector<CardPrice> filterSuppressed(const std::vector<CardPrice>& prices,
                                               const std::vector<std::string>& suppressedVendors) {
    if (suppressedVendors.empty()) {
        return prices;
    }
    std::vector<CardPrice> kept;
    kept.reserve(prices.size());
    for (const CardPrice& p : prices) {
        const bool hidden = std::find(suppressedVendors.begin(), suppressedVendors.end(),
                                      p.provenance) != suppressedVendors.end();
        if (!hidden) {
            kept.push_back(p);
        }
    }
    return kept;
}

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

// The best available price for one vendor, trying `metrics` in preference order. The copy's
// finish (`preferredFinish`) takes priority OVER the metric ranking: a holo copy's holo figure
// from a lower-preference metric still beats a different finish's figure from a higher one —
// the finish is what the user actually owns, the metric is only which estimate we'd rather
// quote. So it first looks for the preferred finish across ALL metrics in order, and only when
// the card lists no row of that finish (or `preferredFinish` is empty) falls back to the
// highest-value row of the most-preferred metric that has any row (any finish). Returns nullptr
// when the vendor has none of the listed metrics, so the headline degrades to what the card
// carries rather than vanishing.
inline const CardPrice* bestPrice(const std::vector<CardPrice>& prices, const char* provenance,
                                  std::initializer_list<const char*> metrics,
                                  const std::string& preferredFinish = "") {
    if (!preferredFinish.empty()) {
        for (const char* metric : metrics) {
            const CardPrice* match = nullptr;  // a row of the copy's actual finish, this metric
            for (const CardPrice& p : prices) {
                if (p.provenance == provenance && p.metric == metric &&
                    p.variant == preferredFinish &&
                    (match == nullptr || p.amountCents > match->amountCents)) {
                    match = &p;
                }
            }
            if (match != nullptr) {
                return match;
            }
        }
    }
    for (const char* metric : metrics) {
        const CardPrice* highest = nullptr;  // the highest of this metric, any finish
        for (const CardPrice& p : prices) {
            if (p.provenance == provenance && p.metric == metric &&
                (highest == nullptr || p.amountCents > highest->amountCents)) {
                highest = &p;
            }
        }
        if (highest != nullptr) {
            return highest;
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
inline VendorBest vendorBest(const std::vector<CardPrice>& prices,
                             const std::string& preferredFinish = "") {
    return {bestPrice(prices, kTcgplayerProvenance, {"market", "mid", "low"}, preferredFinish),
            bestPrice(prices, kCardmarketProvenance,
                      {"trendPrice", "averageSellPrice", "lowPrice"}, preferredFinish)};
}

// The per-card figures with NO vendor labels — just the amounts, each with its currency
// symbol ("$800.43 · €1531.00"). The symbol is the only "context" (it says which
// marketplace: $ = TCGplayer/USD, € = Cardmarket/EUR), so the string fits a narrow table
// cell. Empty when the card has no usable figure. Same per-vendor pick as priceHeadline.
inline QString priceAmountsInline(const std::vector<CardPrice>& prices,
                                  const std::string& preferredFinish = "") {
    const VendorBest best = vendorBest(prices, preferredFinish);
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
inline QString priceHeadline(const std::vector<CardPrice>& prices,
                             const std::string& preferredFinish = "") {
    const VendorBest best = vendorBest(prices, preferredFinish);
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
                                 const std::vector<CardPrice>& prices,
                                 const std::string& preferredFinish = "") {
    const VendorBest best = vendorBest(prices, preferredFinish);
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
