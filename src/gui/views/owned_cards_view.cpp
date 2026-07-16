#include "gui/views/owned_cards_view.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QShowEvent>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <optional>
#include <unordered_map>
#include <vector>

#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_binder.h"
#include "core/domain/card_copy.h"
#include "core/domain/pokemon_catalog.h"
#include "gui/views/binder_picker_dialog.h"
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

OwnedCardsView::OwnedCardsView(CardCopyService& copies, BinderService& binders, QWidget* parent)
    : QWidget(parent), copies_(copies), binders_(binders) {
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search cards…"));  // name, set, number, condition…
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });

    // A read-only eight-column table: dex #, Pokémon, card ref, set, language,
    // condition, ownership, binder. Whole-row selection, no editing; the Pokémon
    // column takes slack. The Set column carries the human set name, which for
    // code-less sets (McDonald's, POP…) is the only disambiguator.
    table_ = new QTableWidget(this);
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon"), tr("Card"), tr("Set"), tr("Lang"),
                                       tr("Condition"), tr("Ownership"), tr("Binder")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    for (int col = 2; col <= 7; ++col) {
        table_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &OwnedCardsView::updateButtonState);

    assignButton_ = new QPushButton(tr("Assign to binder…"), this);
    assignButton_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(assignButton_, &QPushButton::clicked, this, &OwnedCardsView::assignSelected);

    removeButton_ = new QPushButton(tr("Remove…"), this);
    removeButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(removeButton_, &QPushButton::clicked, this, &OwnedCardsView::removeSelected);

    countLabel_ = new QLabel(this);
    countLabel_->setEnabled(false);  // muted: a status detail, not an action

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(assignButton_);
    buttons->addWidget(removeButton_);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    layout->addWidget(search_);
    layout->addWidget(table_);
    layout->addLayout(buttons);
    layout->addWidget(countLabel_);

    updateButtonState();
}

void OwnedCardsView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    reload();  // reflect any copies added since this section was last visible
}

void OwnedCardsView::reload() {
    loaded_ = copies_.listAll();
    // Group a species' copies together, oldest first within a species.
    std::sort(loaded_.begin(), loaded_.end(), [](const CardCopy& a, const CardCopy& b) {
        if (a.pokemonDexNum != b.pokemonDexNum) {
            return a.pokemonDexNum < b.pokemonDexNum;
        }
        return a.insertedAt < b.insertedAt;
    });

    // Resolve a binder id to its display name (re-fetched each reload, so a binder
    // renamed/removed in the Binders section shows correctly here).
    std::unordered_map<std::string, QString> binderNames;
    for (const CardBinder& binder : binders_.list()) {
        binderNames.emplace(binder.id, QString::fromStdString(binder.name));
    }

    table_->setRowCount(static_cast<int>(loaded_.size()));
    haystacks_.assign(loaded_.size(), QString());
    for (int row = 0; row < static_cast<int>(loaded_.size()); ++row) {
        const CardCopy& c = loaded_[row];
        auto* number = cell(QString::number(c.pokemonDexNum));
        number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 0, number);
        table_->setItem(row, 1, cell(speciesName(c.pokemonDexNum)));
        table_->setItem(row, 2, cell(cardText(c.cardRef)));
        table_->setItem(row, 3, cell(QString::fromStdString(c.cardRef.setName)));
        table_->setItem(row, 4, cell(QString::fromStdString(c.cardRef.language)));
        table_->setItem(row, 5, cell(conditionLabel(c.condition)));
        table_->setItem(row, 6, cell(ownershipLabel(c.ownership)));
        QString binderName;
        if (c.binderId) {
            const auto it = binderNames.find(*c.binderId);
            binderName = it != binderNames.end() ? it->second : QString();
        }
        table_->setItem(row, 7, cell(binderName));

        // Precompute this row's lowercased search text from its cells.
        QString hay;
        for (int col = 0; col < table_->columnCount(); ++col) {
            hay += table_->item(row, col)->text() + QLatin1Char(' ');
        }
        haystacks_[row] = hay.toLower();
    }
    applyFilter();  // re-hide non-matches and set the count (search text persists)
    updateButtonState();
}

void OwnedCardsView::applyFilter() {
    // Compare the lowercased needle against each row's precomputed haystack (built
    // in reload()) — a plain substring test per row, no per-keystroke allocation.
    const QString needle = search_->text().trimmed().toLower();
    int visible = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
        const bool match = needle.isEmpty() ||
                           (row < static_cast<int>(haystacks_.size()) &&
                            haystacks_[row].contains(needle));
        table_->setRowHidden(row, !match);
        if (match) {
            ++visible;
        }
    }
    countLabel_->setText(tr("Showing %1 of %2 cards").arg(visible).arg(table_->rowCount()));
}

void OwnedCardsView::updateButtonState() {
    const bool hasSelection = table_->currentRow() >= 0 && !table_->selectedItems().isEmpty();
    assignButton_->setEnabled(hasSelection);
    removeButton_->setEnabled(hasSelection);
}

void OwnedCardsView::assignSelected() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(loaded_.size())) {
        return;
    }
    const CardCopy& copy = loaded_[row];
    BinderPickerDialog dialog(binders_.list(), copy.binderId, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    try {
        copies_.assignToBinder(copy.id, dialog.selectedBinderId());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not file the card:\n%1").arg(QString::fromUtf8(e.what())));
    }
    reload();
}

void OwnedCardsView::removeSelected() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(loaded_.size())) {
        return;
    }
    const CardCopy& copy = loaded_[row];
    // One dialog serves as both the confirmation and the optional note: OK removes
    // (the copy is kept as Removed for auditable history), Cancel aborts. A blank
    // note just removes without appending.
    bool ok = false;
    const QString note = QInputDialog::getMultiLineText(
        this, tr("Remove card"),
        tr("Removing keeps the card in your history as Removed.\n"
           "Optionally add a note (why it left — sold, traded, lost…):"),
        QString(), &ok);
    if (!ok) {
        return;
    }
    try {
        copies_.remove(copy.id, note.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Pokedex TCG"),
            tr("Could not remove the card:\n%1").arg(QString::fromUtf8(e.what())));
    }
    reload();
}

}  // namespace pokedex
