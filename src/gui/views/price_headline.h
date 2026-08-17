#pragma once

#include <QLatin1Char>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QUrl>
#include <QWidget>

#include <algorithm>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_reference.h"
#include "core/domain/types.h"
#include "core/storage/codecs.h"  // timestampToIso
#include "gui/views/glyph_button.h"
#include "gui/views/price_labels.h"

namespace pokedex {

// GUI — the shared "market prices" presentation helpers, drawn on by BOTH the read-only
// inspector summary (CardPricesSummary) and the interactive management page's panel
// (CardPricesPanel). Extracted here so the two can never format money, build a marketplace
// link, or word the ⓘ popover differently — the same reason price_labels.h houses the
// money/vendor-pick helpers. The only difference between the two surfaces is the management
// affordances (per-vendor hide/restore), gated by headlineHtml's `withHideLinks` flag: the
// read-only summary passes false (figures + marketplace links only, suppressed vendors simply
// absent), the page passes true (the ✕/restore that the summary sends the user here to use).

// ISO-8601 UTC (from the timestamp codec) trimmed to its YYYY-MM-DD date — the day
// precision the vendor prices actually carry.
inline QString priceDateOf(Timestamp when) {
    return QString::fromStdString(timestampToIso(when)).left(10);
}

// The newest vendor "as of" moment across a card's cached rows — the date the ⓘ tooltip shows.
// Both price surfaces (the summary and the panel) derive it identically, so it lives here.
// Precondition: `prices` is non-empty (callers only reach it once a headline was built).
inline Timestamp newestObservedAt(const std::vector<CardPrice>& prices) {
    Timestamp newest = prices.front().observedAt;
    for (const CardPrice& p : prices) {
        newest = std::max(newest, p.observedAt);
    }
    return newest;
}

// A marketplace search URL for a card — the click-through under each headline figure.
// tcgdex is addressable by set+number but publishes no stable per-listing URL we carry, so
// the vendor name links to a SEARCH on that marketplace rather than a direct product page.
// Cardmarket's search needs the full set of form params (category=-1 = all categories,
// searchMode=v2): a bare `searchString` alone does not resolve to results.
inline QString marketplaceSearchUrl(const QString& vendor, const QString& searchTerm) {
    // Keep '/' literal (exclude from encoding) so a collector number reads "25/185" in the
    // query, not "25%2F185" — matching what a marketplace search box sends and dodging servers
    // that treat an encoded slash specially. Everything else (spaces, &, …) is still encoded.
    const QString q = QString::fromUtf8(QUrl::toPercentEncoding(searchTerm, "/"));
    if (vendor == QLatin1String("tcgplayer")) {
        return QStringLiteral("https://www.tcgplayer.com/search/pokemon/product?q=%1").arg(q);
    }
    return QStringLiteral("https://www.cardmarket.com/en/Pokemon/Products/Search"
                          "?category=-1&searchString=%1&searchMode=v2")
        .arg(q);
}

// The marketplace search term for a copy — a NAME part plus a SET+NUMBER part, combined so the
// search pins the exact printing ("Charizard VIV 25/185") rather than a name that matches many
// ("Charizard") or a set+number that a marketplace may resolve to the wrong card. The name part
// is the printed card name, else the species/Pokémon name (`speciesName`, blank for a
// species-free Trainer/Energy card); the location part is the set code (or name) + the FULL
// collector number as printed ("25/185"): TCGplayer's search needs the "/total" — "VIV 25"
// matches nothing there while "VIV 25/185" does. (The tcgdex price lookup is separate and uses
// only the printing part "25" for the card id; see resolveTcgdexCardId.) Any part may be absent;
// empty only when the copy records nothing to search by.
inline QString marketSearchTerm(const CardReference& ref, const QString& speciesName) {
    QStringList parts;
    if (!ref.name.empty()) {
        parts << QString::fromStdString(ref.name);
    } else if (!speciesName.isEmpty()) {
        parts << speciesName;
    }
    const std::string set = !ref.expansionCode.empty() ? ref.expansionCode : ref.setName;
    if (!set.empty()) {
        parts << QString::fromStdString(set);
    }
    const QString number = QString::fromStdString(ref.collectorNumber).trimmed();
    if (!number.isEmpty()) {
        parts << number;  // the full "25/185" as printed — TCGplayer needs the /total
    }
    return parts.join(QLatin1Char(' '));
}

// The "ⓘ" popover: what each figure means, so a lone "Cardmarket €26" isn't a mystery.
inline const QString& priceInfoHtml() {
    static const QString kHtml = QStringLiteral(
        "<p><b>What these prices mean</b><br>Aggregated market estimates from two "
        "marketplaces, via the tcgdex pricing provider — a free aggregator, not an official "
        "valuation. Refreshed roughly daily; treat them as rough guidance.</p>"
        "<p><b>The headline</b> shows one figure per source:<br>"
        "• <b>Cardmarket</b> (EUR) — its <i>trend price</i>: an estimate of the current "
        "going rate from recent sales and listings (not a min, median, or max).<br>"
        "• <b>TCGplayer</b> (USD) — its <i>market</i> price: the current market value; when "
        "a card has several finishes the highest such value is shown.</p>"
        "<p>Follow the marketplace links above to search the card for its full price "
        "breakdown and live listings.</p>");
    return kHtml;
}

// The shared "ⓘ" info button both price surfaces carry (the summary and the page's panel): a
// flat tool button that pops the metrics/freshness explanation on click. Defined once here so
// the two can't drift on the same affordance. Callers keep the returned button (to update its
// tooltip per render with freshness) and lay it out themselves; the click reads whatever tooltip
// it currently holds. The button itself is the app-wide glyph affordance (glyph_button.h) — this
// only pins the glyph and the price wording onto it.
inline QToolButton* makeInfoButton(QWidget* parent) {
    return makeGlyphButton(parent, QStringLiteral("ⓘ"), priceInfoHtml(),
                           QObject::tr("What these prices mean"));
}

// The "ⓘ" tooltip/popover text for a priced card: the static metric explanation plus a
// freshness paragraph carrying the vendor "as of" date and the day WE fetched — the
// figures that used to sit on a visible status line now live here, on request.
inline QString priceInfoWithFreshness(const QString& asOf, const QString& fetched) {
    QString html = priceInfoHtml();
    html += QStringLiteral("<p><b>Freshness</b><br>Prices as of %1").arg(asOf);
    if (!fetched.isEmpty()) {
        html += QStringLiteral(", last fetched %1").arg(fetched);
    }
    html += QStringLiteral(".</p>");
    return html;
}

// The headline, as rich text: one representative figure per vendor, one vendor PER LINE, with
// the vendor NAME itself the link to a marketplace search for the card — so the vendor is
// named once (not "TCGplayer $1" plus a separate "Listings: TCGplayer") and the two
// currencies don't crowd one line and wrap unpredictably. Empty only when the card carries no
// usable figure for either shown vendor and (with `withHideLinks`) none is hidden. Only when
// `searchTerm` is blank — no name AND no set/number to search by — are the figures shown as
// plain text.
//
// `withHideLinks` adds the management affordances the prices PAGE owns and the read-only
// inspector summary omits: a muted "✕" after each figure (an in-app "action:hide:<vendor>"
// link) that hides a vendor whose tcgdex mapping is wrong for this card, plus a "… hidden —
// restore" line for each suppressed vendor that DOES carry a price. With it false (the
// summary) suppressed vendors are simply absent — you restore them on the page.
inline QString headlineHtml(const QString& searchTerm, const std::vector<CardPrice>& cached,
                            const std::vector<std::string>& suppressed,
                            const std::string& preferredFinish, bool withHideLinks) {
    // Hold the filtered vector in a named local: vendorBest returns pointers INTO the vector it
    // is given, so passing filterSuppressed(...) inline would dangle them the instant that
    // temporary died (at the `;`), rendering garbage amounts. `cached` is already a named ref.
    const std::vector<CardPrice> shownPrices = filterSuppressed(cached, suppressed);
    const VendorBest shown = vendorBest(shownPrices, preferredFinish);
    const auto isSuppressed = [&](const char* key) {
        return std::find(suppressed.begin(), suppressed.end(), key) != suppressed.end();
    };

    QStringList lines;
    const auto lineFor = [&](const CardPrice* p, const char* vendorKey, const QString& label) {
        if (p == nullptr) {
            return;
        }
        const QString amount = formatMoney(p->amountCents, p->currency).toHtmlEscaped();
        QString namePart;
        if (searchTerm.isEmpty()) {
            namePart = label;
        } else {
            namePart = QStringLiteral("<a href=\"%1\">%2 ↗</a>")
                           .arg(marketplaceSearchUrl(QString::fromLatin1(vendorKey), searchTerm)
                                    .toHtmlEscaped(),
                                label);
        }
        QString line = namePart + QLatin1Char(' ') + amount;
        if (withHideLinks) {
            // A muted "hide this vendor" ✕ after the figure — the user removes a vendor whose
            // tcgdex mapping is wrong for their card. Routed in-app (not a browser link).
            line += QStringLiteral(" <a href=\"action:hide:%1\" style=\"color:gray;"
                                   "text-decoration:none;\" title=\"Hide this vendor\">✕</a>")
                        .arg(QString::fromLatin1(vendorKey));
        }
        lines << line;
    };
    lineFor(shown.tcg, "tcgplayer", QStringLiteral("TCGplayer"));
    lineFor(shown.cm, "cardmarket", QStringLiteral("Cardmarket"));

    if (withHideLinks) {
        // A "restore" line for each hidden vendor that actually carries a cached row (so
        // restoring brings something back) — muted, normal weight, a footnote under the live
        // figures. Only the management page offers this; the summary just omits hidden vendors.
        // Gate on ANY cached row for the vendor, not on vendorBest: a vendor whose rows are all
        // non-headline metrics (e.g. only "high"/"directLow") still needs an in-panel way to be
        // un-hidden — otherwise the suppression is stuck until Clear (which wipes everything).
        const auto hasVendorRow = [&](const char* vendorKey) {
            return std::any_of(cached.begin(), cached.end(),
                               [&](const CardPrice& p) { return p.provenance == vendorKey; });
        };
        const auto restoreFor = [&](const char* vendorKey, const QString& label) {
            if (!isSuppressed(vendorKey) || !hasVendorRow(vendorKey)) {
                return;
            }
            lines << QStringLiteral("<span style=\"color:gray;font-weight:normal;\">%1 hidden — "
                                    "<a href=\"action:show:%2\">restore</a></span>")
                         .arg(label, QString::fromLatin1(vendorKey));
        };
        restoreFor("tcgplayer", QStringLiteral("TCGplayer"));
        restoreFor("cardmarket", QStringLiteral("Cardmarket"));
    }

    return lines.join(QStringLiteral("<br>"));
}

}  // namespace pokedex
