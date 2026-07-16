#include "gui/views/owned_cards_view.h"

#include <QHeaderView>
#include <QLabel>
#include <QShowEvent>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

#include "core/app/card_copy_service.h"
#include "core/domain/card_copy.h"
#include "core/domain/pokemon_catalog.h"
#include "gui/views/condition_labels.h"
#include "gui/views/ownership_labels.h"
#include "gui/views/table_cell.h"

namespace pokedex {

namespace {

// The species name for a dex number, from the compile-time catalog (contiguous
// 1..N, so index == dex - 1). Empty for an out-of-range number (defensive).
QString speciesName(PokemonDexNum dexNumber) {
    const auto catalog = pokemonCatalog();
    if (dexNumber < 1 || dexNumber > static_cast<int>(catalog.size())) {
        return QString();
    }
    return QString::fromStdString(catalog[dexNumber - 1].name);
}

// The printed identity as one cell: "BS 44/102", or just the number when the
// expansion code is unknown.
QString cardText(const CardReference& ref) {
    const QString expansion = QString::fromStdString(ref.expansionCode);
    const QString number = QString::fromStdString(ref.collectorNumber);
    return expansion.isEmpty() ? number : expansion + QStringLiteral(" ") + number;
}

}  // namespace

OwnedCardsView::OwnedCardsView(CardCopyService& copies, QWidget* parent)
    : QWidget(parent), copies_(copies) {
    // A read-only six-column table: dex #, Pokémon, card ref, language, condition,
    // ownership. Whole-row selection, no editing; the Pokémon column takes slack.
    table_ = new QTableWidget(this);
    table_->setColumnCount(6);
    table_->setHorizontalHeaderLabels(
        {tr("#"), tr("Pokémon"), tr("Card"), tr("Lang"), tr("Condition"), tr("Ownership")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

    countLabel_ = new QLabel(this);
    countLabel_->setEnabled(false);  // muted: a status detail, not an action

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    layout->addWidget(table_);
    layout->addWidget(countLabel_);
}

void OwnedCardsView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    reload();  // reflect any copies added since this section was last visible
}

void OwnedCardsView::reload() {
    std::vector<CardCopy> copies = copies_.listAll();
    // Group a species' copies together, oldest first within a species.
    std::sort(copies.begin(), copies.end(), [](const CardCopy& a, const CardCopy& b) {
        if (a.pokemonDexNum != b.pokemonDexNum) {
            return a.pokemonDexNum < b.pokemonDexNum;
        }
        return a.insertedAt < b.insertedAt;
    });

    table_->setRowCount(static_cast<int>(copies.size()));
    for (int row = 0; row < static_cast<int>(copies.size()); ++row) {
        const CardCopy& c = copies[row];
        auto* number = cell(QString::number(c.pokemonDexNum));
        number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 0, number);
        table_->setItem(row, 1, cell(speciesName(c.pokemonDexNum)));
        table_->setItem(row, 2, cell(cardText(c.cardRef)));
        table_->setItem(row, 3, cell(QString::fromStdString(c.cardRef.language)));
        table_->setItem(row, 4, cell(conditionLabel(c.condition)));
        table_->setItem(row, 5, cell(ownershipLabel(c.ownership)));
    }
    countLabel_->setText(tr("%1 cards in your collection").arg(copies.size()));
}

}  // namespace pokedex
