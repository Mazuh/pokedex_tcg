#include "gui/views/card_prices_panel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QToolTip>
#include <QVBoxLayout>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/storage/codecs.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/price_labels.h"
#include "gui/views/sortable_table.h"

namespace pokedex {

namespace {

// ISO-8601 UTC (from the timestamp codec) trimmed to its YYYY-MM-DD date — the day
// precision the vendor prices actually carry.
QString dateOf(Timestamp when) {
    return QString::fromStdString(timestampToIso(when)).left(10);
}

// The "ⓘ" popover: what each figure means, so a lone "Cardmarket €26" isn't a mystery.
const QString& priceInfoHtml() {
    static const QString kHtml = QStringLiteral(
        "<p><b>What these prices mean</b><br>Aggregated market estimates from two "
        "marketplaces, refreshed roughly daily — treat them as rough guidance, not a "
        "fixed value.</p>"
        "<p><b>The headline</b> shows one figure per source:<br>"
        "• <b>Cardmarket</b> (EUR) — its <i>trend price</i>: an estimate of the current "
        "going rate from recent sales and listings (not a min, median, or max).<br>"
        "• <b>TCGplayer</b> (USD) — its <i>market</i> price: the current market value; when "
        "a card has several finishes the highest such value is shown.</p>"
        "<p><b>Show all prices</b> expands the full spread — Cardmarket <i>low</i> "
        "(cheapest listing), <i>average sell</i> (mean of real sales), and 1/7/30-day "
        "averages; TCGplayer <i>low</i>/<i>mid</i>/<i>market</i>/<i>direct-low</i> per "
        "finish.</p>"
        "<p>The TCGplayer <i>high</i> (a single top listing) is deliberately hidden — it "
        "is routinely an unrealistic outlier.</p>");
    return kHtml;
}

// The listing links for the vendors present in `prices`, as rich text opening the real
// marketplace page. pokemontcg.io serves a stable per-vendor redirect keyed by the card
// id (verified to 302 → the TCGplayer product page / the exact Cardmarket single), so
// the URL is built from the id with no extra request or stored field.
QString listingLinksHtml(const std::string& externalCardId, const std::vector<CardPrice>& prices) {
    bool hasTcg = false;
    bool hasCm = false;
    for (const CardPrice& p : prices) {
        hasTcg = hasTcg || p.provenance == kTcgplayerProvenance;
        hasCm = hasCm || p.provenance == kCardmarketProvenance;
    }
    const QString id = QString::fromStdString(externalCardId);
    QStringList parts;
    if (hasTcg) {
        parts << QStringLiteral(
                     "<a href=\"https://prices.pokemontcg.io/tcgplayer/%1\">TCGplayer ↗</a>")
                     .arg(id);
    }
    if (hasCm) {
        parts << QStringLiteral(
                     "<a href=\"https://prices.pokemontcg.io/cardmarket/%1\">Cardmarket ↗</a>")
                     .arg(id);
    }
    if (parts.isEmpty()) {
        return {};
    }
    return QStringLiteral("Listings: ") + parts.join(QStringLiteral(" · "));
}

}  // namespace

CardPricesPanel::CardPricesPanel(CardPriceLookupService& lookup, QWidget* parent)
    : QWidget(parent), lookup_(lookup) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    headline_ = new QLabel(this);
    headline_->setWordWrap(true);
    headline_->setStyleSheet(QStringLiteral("font-weight: 600;"));

    // "ⓘ" popover explaining the metrics — the same idiom the card-attribute pickers use.
    infoButton_ = new QToolButton(this);
    infoButton_->setText(QStringLiteral("ⓘ"));
    infoButton_->setAutoRaise(true);
    infoButton_->setFocusPolicy(Qt::NoFocus);
    infoButton_->setCursor(Qt::WhatsThisCursor);
    infoButton_->setToolTip(priceInfoHtml());
    infoButton_->setAccessibleName(tr("What these prices mean"));
    connect(infoButton_, &QToolButton::clicked, this, [this]() {
        QToolTip::showText(infoButton_->mapToGlobal(QPoint(0, infoButton_->height())),
                           priceInfoHtml(), infoButton_);
    });

    auto* headlineRow = new QHBoxLayout;
    headlineRow->setContentsMargins(0, 0, 0, 0);
    headlineRow->addWidget(headline_);
    headlineRow->addWidget(infoButton_);
    headlineRow->addStretch(1);

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: gray;"));

    // Listing links (rich text) that open the real marketplace pages in the browser.
    links_ = new QLabel(this);
    links_->setTextFormat(Qt::RichText);
    links_->setOpenExternalLinks(true);
    links_->setWordWrap(true);

    auto* buttonRow = new QHBoxLayout;
    fetchButton_ = new QPushButton(this);
    toggle_ = new QToolButton(this);
    toggle_->setCheckable(true);
    toggle_->setText(QStringLiteral("Show all prices ▸"));
    buttonRow->addWidget(fetchButton_);
    buttonRow->addWidget(toggle_);
    buttonRow->addStretch(1);

    table_ = new QTableWidget(this);
    table_->setColumnCount(5);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("Source"), QStringLiteral("Variant"), QStringLiteral("Metric"),
         QStringLiteral("Price"), QStringLiteral("As of")});
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setVisible(false);
    // Header-click sorting (the repo convention for every table): a click reorders the
    // cached rows_ in memory and refills — never Qt's setSortingEnabled.
    installHeaderSort(table_, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        sortOrder_ = order;
        repopulateTable();
    });

    layout->addLayout(headlineRow);
    layout->addWidget(status_);
    layout->addWidget(links_);
    layout->addLayout(buttonRow);
    layout->addWidget(table_);

    connect(fetchButton_, &QPushButton::clicked, this, &CardPricesPanel::onFetchClicked);
    connect(toggle_, &QToolButton::toggled, this, [this](bool on) {
        table_->setVisible(on);
        toggle_->setText(on ? QStringLiteral("Hide prices ▾") : QStringLiteral("Show all prices ▸"));
    });
    // Re-render when a fetch we (or another view) triggered lands for our card.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            render();
        }
    });
    connect(&lookup_, &CardPriceLookupService::pricesFailed, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            fetchButton_->setEnabled(true);
            // Neutral wording: the failure may be a busy/flaky API OR a card the
            // catalog no longer lists (a 404, which the transport fails fast) — don't
            // assert "try again" when a retry may never help.
            status_->setText(QStringLiteral("Couldn't fetch prices for this card right now."));
        }
    });
}

void CardPricesPanel::showCard(const QString& externalCardId) {
    externalCardId_ = externalCardId;
    fetching_ = false;
    toggle_->setChecked(false);
    render();
}

void CardPricesPanel::render() {
    if (fetching_) {
        return;  // onFetchClicked owns the UI until the reply lands
    }

    if (externalCardId_.isEmpty()) {
        rows_.clear();
        headline_->hide();
        infoButton_->hide();
        links_->hide();
        status_->setText(QStringLiteral("Not linked to a catalog card, so prices can't be looked "
                                        "up. Cards added from the card finder are linked."));
        status_->show();
        fetchButton_->hide();
        toggle_->hide();
        table_->hide();
        return;
    }

    // Read the cache defensively: render() runs from a selection-change slot, and a DB
    // read can throw (e.g. a second app instance holding the SQLite file lock, which
    // CLAUDE.md warns about). An exception escaping a Qt slot calls std::terminate — so
    // degrade to a message rather than crash the app on select.
    std::vector<CardPrice> cached;
    std::optional<Timestamp> fetchedAt;
    try {
        cached = lookup_.cached(externalCardId_);
        fetchedAt = lookup_.fetchedAt(externalCardId_);
    } catch (const std::exception&) {
        rows_.clear();
        headline_->hide();
        infoButton_->hide();
        links_->hide();
        toggle_->hide();
        table_->hide();
        fetchButton_->hide();
        status_->setText(QStringLiteral("Couldn't read the stored prices."));
        status_->show();
        return;
    }

    fetchButton_->show();
    fetchButton_->setEnabled(true);

    // Emptiness is decided on the RAW cache, before hiding the "high" outlier — else a
    // card whose only cached metric is TCGplayer "high" would falsely read as priceless.
    if (cached.empty()) {
        rows_.clear();
        headline_->hide();
        infoButton_->hide();
        links_->hide();
        toggle_->hide();
        table_->hide();
        if (fetchedAt) {
            status_->setText(QStringLiteral("No market prices found for this card."));
            fetchButton_->setText(QStringLiteral("Refresh"));
        } else {
            status_->setText(QStringLiteral("Prices not fetched yet."));
            fetchButton_->setText(QStringLiteral("Fetch prices"));
        }
        status_->show();
        return;
    }

    // Drop the TCGplayer "high" (a single top listing, a routinely unrealistic outlier)
    // — but only when that still leaves a price to show; if "high" is the sole cached
    // metric, showing it beats claiming the card has no price. (std::erase_if — C++20.)
    rows_ = cached;
    std::vector<CardPrice> withoutHigh = cached;
    std::erase_if(withoutHigh, [](const CardPrice& p) {
        return p.provenance == kTcgplayerProvenance && p.metric == "high";
    });
    if (!withoutHigh.empty()) {
        rows_ = std::move(withoutHigh);
    }

    const QString headline = priceHeadline(rows_);
    headline_->setText(headline.isEmpty() ? QStringLiteral("Market prices") : headline);
    headline_->show();
    infoButton_->show();

    // Links to the vendors' own listing pages, for whichever vendors returned prices.
    const QString linksHtml = listingLinksHtml(externalCardId_.toStdString(), rows_);
    links_->setText(linksHtml);
    links_->setVisible(!linksHtml.isEmpty());

    // "as of" is the newest vendor date across the rows; also show when WE fetched.
    Timestamp newest = rows_.front().observedAt;
    for (const CardPrice& p : rows_) {
        newest = std::max(newest, p.observedAt);
    }
    QString status = QStringLiteral("as of %1").arg(dateOf(newest));
    if (fetchedAt) {
        status += QStringLiteral(" · fetched %1").arg(dateOf(*fetchedAt));
    }
    status_->setText(status);
    status_->show();
    fetchButton_->setText(QStringLiteral("Refresh"));

    repopulateTable();
    toggle_->show();
    table_->setVisible(toggle_->isChecked());
}

void CardPricesPanel::repopulateTable() {
    // A header click is a pure in-memory reorder of rows_ (never Qt's row sorting) —
    // sortColumn_ < 0 keeps the natural (provenance, variant, metric) load order.
    applyColumnSort(rows_, sortColumn_, sortOrder_,
                    [](const CardPrice& a, const CardPrice& b, int col) -> int {
                        switch (col) {
                            case 0:
                                return QString::fromStdString(a.provenance)
                                    .localeAwareCompare(QString::fromStdString(b.provenance));
                            case 1:
                                return QString::fromStdString(a.variant)
                                    .localeAwareCompare(QString::fromStdString(b.variant));
                            case 2:
                                return QString::fromStdString(a.metric)
                                    .localeAwareCompare(QString::fromStdString(b.metric));
                            case 3:
                                return compareValues(a.amountCents, b.amountCents);
                            case 4:
                                return compareValues(a.observedAt, b.observedAt);
                            default:
                                return 0;
                        }
                    });

    table_->setRowCount(static_cast<int>(rows_.size()));
    for (int row = 0; row < static_cast<int>(rows_.size()); ++row) {
        const CardPrice& p = rows_[static_cast<std::size_t>(row)];
        table_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(p.provenance)));
        table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(p.variant)));
        table_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(p.metric)));
        table_->setItem(row, 3, new QTableWidgetItem(formatMoney(p.amountCents, p.currency)));
        table_->setItem(row, 4, new QTableWidgetItem(dateOf(p.observedAt)));
    }
    table_->resizeColumnsToContents();
}

void CardPricesPanel::onFetchClicked() {
    if (externalCardId_.isEmpty()) {
        return;
    }
    fetching_ = true;
    fetchButton_->setEnabled(false);
    status_->setText(QStringLiteral("Fetching prices…"));
    status_->show();
    // Force a network fetch — the button is an explicit user request for the latest.
    lookup_.fetch(externalCardId_, /*force=*/true);
}

}  // namespace pokedex
