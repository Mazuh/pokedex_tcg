#include "gui/views/card_prices_panel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_copy_service.h"
#include "core/app/card_price_dto.h"
#include "core/domain/card_copy.h"
#include "core/storage/codecs.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/price_labels.h"

namespace pokedex {

namespace {

// ISO-8601 UTC (from the timestamp codec) trimmed to its YYYY-MM-DD date — the day
// precision the vendor prices actually carry.
QString dateOf(Timestamp when) {
    return QString::fromStdString(timestampToIso(when)).left(10);
}

// A marketplace search URL for a card name — the click-through under each headline figure.
// tcgdex is addressable by set+number but publishes no stable per-listing URL we carry, so
// the vendor name links to a NAME SEARCH on that marketplace (always valid) rather than a
// direct product page.
QString marketplaceSearchUrl(const QString& vendor, const QString& cardName) {
    const QString q = QString::fromUtf8(QUrl::toPercentEncoding(cardName));
    if (vendor == QLatin1String("tcgplayer")) {
        return QStringLiteral("https://www.tcgplayer.com/search/pokemon/product?q=%1").arg(q);
    }
    return QStringLiteral("https://www.cardmarket.com/en/Pokemon/Products/Search?searchString=%1")
        .arg(q);
}

// The "ⓘ" popover: what each figure means, so a lone "Cardmarket €26" isn't a mystery.
const QString& priceInfoHtml() {
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

// The headline, as rich text: one representative figure per vendor, one vendor PER LINE, with
// the vendor NAME itself the link to a marketplace search for the card — so the vendor is
// named once (not "TCGplayer $1" plus a separate "Listings: TCGplayer") and the two
// currencies don't crowd one line and wrap unpredictably. Empty only when the card carries no
// usable figure for either vendor (the caller then shows a plain "Market prices" label). When
// no card name is known the figures are shown as plain text (nothing to search by).
QString linkedHeadlineHtml(const QString& cardName, const std::vector<CardPrice>& prices) {
    const VendorBest best = vendorBest(prices);
    QStringList lines;
    const auto lineFor = [&](const CardPrice* p, const char* vendorKey, const QString& label) {
        if (p == nullptr) {
            return;
        }
        const QString amount = formatMoney(p->amountCents, p->currency).toHtmlEscaped();
        if (cardName.isEmpty()) {
            lines << QStringLiteral("%1 %2").arg(label, amount);
        } else {
            lines << QStringLiteral("<a href=\"%1\">%2 ↗</a> %3")
                         .arg(marketplaceSearchUrl(QString::fromLatin1(vendorKey), cardName).toHtmlEscaped(),
                              label, amount);
        }
    };
    lineFor(best.tcg, "tcgplayer", QStringLiteral("TCGplayer"));
    lineFor(best.cm, "cardmarket", QStringLiteral("Cardmarket"));
    return lines.join(QStringLiteral("<br>"));
}

// The "ⓘ" tooltip/popover text for a priced card: the static metric explanation plus a
// freshness paragraph carrying the vendor "as of" date and the day WE fetched — the
// figures that used to sit on a visible status line now live here, on request.
QString priceInfoWithFreshness(const QString& asOf, const QString& fetched) {
    QString html = priceInfoHtml();
    html += QStringLiteral("<p><b>Freshness</b><br>Prices as of %1").arg(asOf);
    if (!fetched.isEmpty()) {
        html += QStringLiteral(", last fetched %1").arg(fetched);
    }
    html += QStringLiteral(".</p>");
    return html;
}

}  // namespace

CardPricesPanel::CardPricesPanel(CardPriceLookupService& lookup, CardCopyService& copies,
                                 QWidget* parent)
    : QWidget(parent), lookup_(lookup), copies_(copies) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // The headline carries the per-vendor figures with the vendor name linking to a
    // marketplace search (rich text), so a vendor is named once rather than repeated in a
    // separate "Listings" line.
    headline_ = new QLabel(this);
    headline_->setWordWrap(true);
    headline_->setTextFormat(Qt::RichText);
    headline_->setOpenExternalLinks(true);
    headline_->setStyleSheet(QStringLiteral("font-weight: 600;"));

    // "ⓘ" popover explaining the metrics — the same idiom the card-attribute pickers use.
    // Its tooltip also carries the price freshness (vendor "as of" + our fetch date), set
    // per-render; the click shows whatever the current tooltip holds.
    infoButton_ = new QToolButton(this);
    infoButton_->setText(QStringLiteral("ⓘ"));
    infoButton_->setAutoRaise(true);
    infoButton_->setFocusPolicy(Qt::NoFocus);
    infoButton_->setCursor(Qt::WhatsThisCursor);
    infoButton_->setToolTip(priceInfoHtml());
    infoButton_->setAccessibleName(tr("What these prices mean"));
    connect(infoButton_, &QToolButton::clicked, this, [this]() {
        QToolTip::showText(infoButton_->mapToGlobal(QPoint(0, infoButton_->height())),
                           infoButton_->toolTip(), infoButton_);
    });

    // The ⓘ sits at the far right, top-aligned so it pairs with the first headline line
    // (the headline can span two lines, one per vendor).
    auto* headlineRow = new QHBoxLayout;
    headlineRow->setContentsMargins(0, 0, 0, 0);
    headlineRow->addWidget(headline_, /*stretch=*/1);
    headlineRow->addWidget(infoButton_, 0, Qt::AlignTop | Qt::AlignRight);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: gray;"));

    auto* buttonRow = new QHBoxLayout;
    fetchButton_ = new QPushButton(this);
    buttonRow->addWidget(fetchButton_);
    buttonRow->addStretch(1);

    layout->addLayout(headlineRow);
    layout->addWidget(status_);
    layout->addLayout(buttonRow);

    connect(fetchButton_, &QPushButton::clicked, this, &CardPricesPanel::onFetchClicked);
    // Re-render when a fetch we (or another view) triggered lands for our card.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            render();
        }
    });
    connect(&lookup_, &CardPriceLookupService::pricesFailed, this, [this](const QString& id) {
        if (id != externalCardId_) {
            return;
        }
        if (!fetching_) {
            // A background fetch for this same card, started by ANOTHER panel (the lookup
            // service is app-wide and its signal carries only the id), failed. We never
            // asked, and a failed fetch changes no cache — so leave our current display
            // (valid cached prices and their "as of" line) untouched rather than painting a
            // contradictory error over it.
            return;
        }
        fetching_ = false;
        fetchButton_->setEnabled(true);
        // Neutral wording: the failure may be a busy/flaky API OR a card the provider does
        // not list (a 404, which the transport fails fast) — don't assert "try again" when a
        // retry may never help.
        status_->setText(QStringLiteral("Couldn't fetch prices for this card right now."));
        status_->show();
    });

    // When the (lazily fetched) tcgdex set table becomes available, a Fetch that was waiting
    // on it can resolve the card and go on to fetch prices. The signal is app-wide, so act
    // only if THIS panel is the one waiting.
    connect(&lookup_, &CardPriceLookupService::tcgdexSetsResolved, this, [this](bool ok) {
        if (!awaitingSets_) {
            return;
        }
        awaitingSets_ = false;
        if (ok) {
            resolveAndFetch();
            return;
        }
        // The set table is unavailable. If the copy already carries an id, fetch it as a
        // best effort; otherwise there is nothing to look up — offer a retry.
        if (!externalCardId_.isEmpty()) {
            lookup_.fetch(externalCardId_);
            return;
        }
        fetching_ = false;
        fetchButton_->setEnabled(true);
        status_->setText(QStringLiteral("Couldn't reach the pricing catalog. Please try again."));
        status_->show();
    });
}

void CardPricesPanel::showCopy(const CardCopy& copy) {
    copyId_ = copy.id;
    cardRef_ = copy.cardRef;
    externalCardId_ = QString::fromStdString(copy.externalCardId);
    copyRemoved_ = copy.ownership == CardOwnership::Removed;
    fetching_ = false;
    awaitingSets_ = false;
    render();
}

void CardPricesPanel::clear() {
    copyId_.clear();
    cardRef_ = CardReference{};
    externalCardId_.clear();
    copyRemoved_ = false;
    fetching_ = false;
    awaitingSets_ = false;
    render();
}

bool CardPricesPanel::canResolve() const {
    // A soft-Removed copy is frozen history: never spend a resolve + fetch on a discarded
    // card. Otherwise a set (name or printed code) plus a collector number is all tcgdex needs
    // to address the card by set+number.
    if (copyRemoved_ || copyId_.empty()) {
        return false;
    }
    const bool hasSet = !cardRef_.setName.empty() || !cardRef_.expansionCode.empty();
    const bool hasNumber = !cardRef_.collectorNumber.empty();
    return hasSet && hasNumber;
}

void CardPricesPanel::resetToMessage(const QString& text) {
    headline_->hide();
    infoButton_->hide();
    fetchButton_->hide();
    if (text.isEmpty()) {
        status_->hide();
    } else {
        status_->setText(text);
        status_->show();
    }
}

void CardPricesPanel::showFetchAffordance(const QString& message, const QString& buttonText) {
    resetToMessage(message);  // hides the button…
    fetchButton_->setText(buttonText);
    fetchButton_->show();  // …so re-show + enable it here
    fetchButton_->setEnabled(true);
}

void CardPricesPanel::render() {
    if (fetching_) {
        return;  // onFetchClicked owns the UI until the reply lands
    }

    if (copyId_.empty()) {
        resetToMessage(QString());  // nothing selected
        return;
    }
    if (copyRemoved_) {
        resetToMessage(QString());  // frozen history — no price affordance at all
        return;
    }

    const bool resolvable = canResolve();
    if (externalCardId_.isEmpty() && !resolvable) {
        // Nothing to look up: too little data to resolve a tcgdex card. Point to Edit to
        // complete it. (Linking itself is never named as a user action.)
        resetToMessage(QStringLiteral("Add this card's set and collector number (via “Edit "
                                      "card…”) to look up its market prices."));
        return;
    }
    if (externalCardId_.isEmpty()) {
        // Resolvable but never linked/fetched: present exactly like a linked-unfetched card.
        // The first fetch resolves and persists the link invisibly (onFetchClicked).
        showFetchAffordance(QStringLiteral("Prices not fetched yet."),
                            QStringLiteral("Fetch prices"));
        return;
    }

    // Read the cache defensively: render() runs from a selection-change slot, and a DB
    // read can throw (e.g. a second app instance holding the SQLite file lock, which
    // CLAUDE.md warns about). An exception escaping a Qt slot calls std::terminate — so
    // degrade to a message rather than crash the app on select. One combined read for both
    // the prices and the fetch stamp (see cachedPrices), so a selection consults the cache
    // through one call and one error path.
    std::vector<CardPrice> cached;
    std::optional<Timestamp> fetchedAt;
    try {
        CardPriceLookupService::CachedPrices snapshot = lookup_.cachedPrices(externalCardId_);
        cached = std::move(snapshot.prices);
        fetchedAt = snapshot.fetchedAt;
    } catch (const std::exception&) {
        resetToMessage(QStringLiteral("Couldn't read the stored prices."));
        return;
    }

    if (cached.empty()) {
        // A message with a live Fetch/Refresh button so the user can (re)fetch.
        showFetchAffordance(fetchedAt ? QStringLiteral("No market prices found for this card.")
                                      : QStringLiteral("Prices not fetched yet."),
                            fetchedAt ? QStringLiteral("Refresh")
                                      : QStringLiteral("Fetch prices"));
        return;
    }

    // The headline names each vendor once, the name itself linking to a marketplace search —
    // one representative figure per source (vendorBest), so the full per-metric spread is
    // left to the marketplace rather than shown as a raw cache table. (vendorBest never
    // picks the TCGplayer "high" outlier, so it can't surface here.)
    const QString headline =
        linkedHeadlineHtml(QString::fromStdString(cardRef_.name), cached);
    headline_->setText(headline.isEmpty() ? QStringLiteral("Market prices") : headline);
    headline_->show();

    // "as of" is the newest vendor date across the rows; also carry when WE fetched. Both
    // move onto the ⓘ tooltip (no separate visible line), so a glance stays uncluttered.
    Timestamp newest = cached.front().observedAt;
    for (const CardPrice& p : cached) {
        newest = std::max(newest, p.observedAt);
    }
    infoButton_->setToolTip(priceInfoWithFreshness(
        dateOf(newest), fetchedAt ? dateOf(*fetchedAt) : QString()));
    infoButton_->show();
    status_->hide();  // freshness lives on the ⓘ tooltip now, not a visible line
    fetchButton_->show();
    fetchButton_->setEnabled(true);
    fetchButton_->setText(QStringLiteral("Refresh"));
}

void CardPricesPanel::onFetchClicked() {
    if (fetching_) {
        return;  // a fetch is already in flight
    }
    if (canResolve()) {
        // Resolve the tcgdex card id from the copy's set+number, then fetch. This both links
        // an unlinked copy and re-resolves one still on a pre-tcgdex id — invisibly.
        fetching_ = true;
        fetchButton_->setEnabled(false);
        status_->setText(QStringLiteral("Looking up this card…"));
        status_->show();
        if (lookup_.tcgdexSetsReady()) {
            resolveAndFetch();
        } else {
            awaitingSets_ = true;
            lookup_.ensureTcgdexSets();  // tcgdexSetsResolved → resolveAndFetch()
        }
        return;
    }
    if (!externalCardId_.isEmpty()) {
        // Not resolvable from what the copy records, but it already carries an id — fetch it
        // directly. The button is an explicit user request for the latest, so hit the wire.
        fetching_ = true;
        fetchButton_->setEnabled(false);
        status_->setText(QStringLiteral("Fetching prices…"));
        status_->show();
        lookup_.fetch(externalCardId_);
    }
}

void CardPricesPanel::resolveAndFetch() {
    const std::optional<QString> id = lookup_.resolveTcgdexId(cardRef_);
    if (!id) {
        // The set table is loaded but this copy's set/number couldn't be exactly identified.
        // Do NOT fall back to fetching externalCardId_: a legitimately linked copy's tcgdex id
        // would have re-resolved here, so a non-empty id at this point is a stale/foreign key
        // (e.g. a pre-tcgdex pokemontcg id) whose GET would 404 with a misleading error. Report
        // the accurate guidance instead.
        fetching_ = false;
        fetchButton_->setEnabled(true);
        status_->setText(QStringLiteral("Couldn't identify this card for pricing — check its "
                                        "set and collector number in “Edit card…”."));
        status_->show();
        return;
    }

    if (*id != externalCardId_) {
        // Persist the resolved link (new, or migrated off a pre-tcgdex id) before fetching, so
        // a re-selection sees the copy as linked and the cache keys to the tcgdex id.
        try {
            copies_.linkCatalogCard(copyId_, id->toStdString());
        } catch (const std::exception& e) {
            fetching_ = false;
            fetchButton_->setEnabled(true);
            status_->setText(QStringLiteral("Couldn't look up this card right now."));
            status_->show();
            QMessageBox::warning(this, tr("Pokedex TCG"),
                                 tr("Could not link this card:\n%1").arg(QString::fromUtf8(e.what())));
            return;
        }
        externalCardId_ = *id;
        Q_EMIT cardLinked(QString::fromStdString(copyId_), externalCardId_);
    }
    lookup_.fetch(externalCardId_);  // pricesReady → fetching_=false, render()
}

}  // namespace pokedex
