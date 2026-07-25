#include "gui/views/card_prices_panel.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
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

}  // namespace

CardPricesPanel::CardPricesPanel(CardPriceLookupService& lookup, QWidget* parent)
    : QWidget(parent), lookup_(lookup) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    headline_ = new QLabel(this);
    headline_->setWordWrap(true);
    headline_->setStyleSheet(QStringLiteral("font-weight: 600;"));

    status_ = new QLabel(this);
    status_->setWordWrap(true);
    status_->setStyleSheet(QStringLiteral("color: gray;"));

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

    layout->addWidget(headline_);
    layout->addWidget(status_);
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
        status_->setText(QStringLiteral("Not linked to a catalog card, so prices can't be looked "
                                        "up. Cards added from the card finder are linked."));
        status_->show();
        fetchButton_->hide();
        toggle_->hide();
        table_->hide();
        return;
    }

    rows_ = lookup_.cached(externalCardId_);
    fetchButton_->show();
    fetchButton_->setEnabled(true);

    if (rows_.empty()) {
        headline_->hide();
        toggle_->hide();
        table_->hide();
        if (lookup_.fetchedAt(externalCardId_)) {
            status_->setText(QStringLiteral("No market prices found for this card."));
            fetchButton_->setText(QStringLiteral("Refresh"));
        } else {
            status_->setText(QStringLiteral("Prices not fetched yet."));
            fetchButton_->setText(QStringLiteral("Fetch prices"));
        }
        status_->show();
        return;
    }

    const QString headline = priceHeadline(rows_);
    headline_->setText(headline.isEmpty() ? QStringLiteral("Market prices") : headline);
    headline_->show();

    // "as of" is the newest vendor date across the rows; also show when WE fetched.
    Timestamp newest = rows_.front().observedAt;
    for (const CardPrice& p : rows_) {
        newest = std::max(newest, p.observedAt);
    }
    QString status = QStringLiteral("as of %1").arg(dateOf(newest));
    if (const auto fetched = lookup_.fetchedAt(externalCardId_)) {
        status += QStringLiteral(" · fetched %1").arg(dateOf(*fetched));
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
