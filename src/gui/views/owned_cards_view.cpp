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
#include "gui/views/region_labels.h"
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

// The region label for a dex number, from the same compile-time catalog. Not
// shown in any column, but appended to the search haystack so a copy is findable
// by its species' region. Empty for an out-of-range number (defensive).
QString speciesRegionLabel(PokemonDexNum dexNumber) {
    const auto catalog = pokemonCatalog();
    if (dexNumber < 1 || dexNumber > static_cast<int>(catalog.size())) {
        return QString();
    }
    return regionLabel(catalog[dexNumber - 1].region);
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
    search_->setPlaceholderText(
        tr("Search copy by Pokémon, collector number, set or binder…"));
    search_->setClearButtonEnabled(true);
    connect(search_, &QLineEdit::textChanged, this, [this](const QString&) { applyFilter(); });

    // A read-only seven-column table: Pokémon, card ref, set, language,
    // condition, ownership, binder. Whole-row selection, no editing; the Pokémon
    // column stretches to take up slack, while every data column sizes to its
    // content. The Set column carries the human set name, which for code-less sets
    // (McDonald's, POP…) is the only disambiguator.
    table_ = new QTableWidget(this);
    table_->setColumnCount(7);
    table_->setHorizontalHeaderLabels({tr("Pokémon"), tr("Card"), tr("Set"), tr("Lang"),
                                       tr("Cond."), tr("Ownership"), tr("Binder")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    auto* header = table_->horizontalHeader();
    // Pokémon (the primary, recognizable column) is the flexible one: it stretches
    // to absorb slack and elides when space is tight. (Previously Binder was the
    // Stretch column, so a narrow window shrank it to nothing *and* left no overflow
    // to scroll to.) The short metadata columns size to their content. Set and
    // Binder hold free text — a long set or user-named binder — so they are sized to
    // content but capped in reload() (Interactive lets reload() set their width and
    // keeps them user-draggable), so one long name can't crowd out Pokémon.
    header->setSectionResizeMode(0, QHeaderView::Stretch);
    for (const int col : {1, 3, 4, 5}) {  // Card, Lang, Cond., Ownership — short
        header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    header->setSectionResizeMode(2, QHeaderView::Interactive);  // Set — free text, capped
    header->setSectionResizeMode(6, QHeaderView::Interactive);  // Binder — free text, capped
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &OwnedCardsView::updateButtonState);

    assignButton_ = new QPushButton(tr("Assign to binder…"), this);
    assignButton_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    connect(assignButton_, &QPushButton::clicked, this, &OwnedCardsView::assignSelected);

    removeButton_ = new QPushButton(tr("Remove…"), this);
    removeButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    connect(removeButton_, &QPushButton::clicked, this, &OwnedCardsView::removeSelected);

    // Shown in place of the table (and search) when the collection is empty.
    emptyLabel_ = new QLabel(
        tr("No cards yet. Open a Pokémon in “All Pokémon” and use “Add copy…” to "
           "record one."),
        this);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    emptyLabel_->setEnabled(false);  // muted: a hint, not content

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
    layout->addWidget(emptyLabel_);
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

    // Resolve a binder id to its display name (shown in the Binder column) and its
    // region (search-only) in one lookup — re-fetched each reload, so a binder
    // renamed/removed in the Binders section shows correctly here.
    struct BinderInfo {
        QString name;
        QString region;  // empty when the binder wasn't scoped to a region
    };
    std::unordered_map<std::string, BinderInfo> binderInfo;
    for (const CardBinder& binder : binders_.list()) {
        binderInfo.emplace(binder.id,
                           BinderInfo{QString::fromStdString(binder.name),
                                      binder.pokemonRegion ? regionLabel(*binder.pokemonRegion)
                                                           : QString()});
    }

    table_->setRowCount(static_cast<int>(loaded_.size()));
    haystacks_.assign(loaded_.size(), QString());
    for (int row = 0; row < static_cast<int>(loaded_.size()); ++row) {
        const CardCopy& c = loaded_[row];
        table_->setItem(row, 0, cell(speciesName(c.pokemonDexNum)));
        table_->setItem(row, 1, cell(cardText(c.cardRef)));
        // Set and Binder are the free-text, capped columns: carry the full value as
        // a tooltip so it stays readable when the cap elides it.
        auto* setCell = cell(QString::fromStdString(c.cardRef.setName));
        setCell->setToolTip(QString::fromStdString(c.cardRef.setName));
        table_->setItem(row, 2, setCell);
        table_->setItem(row, 3, cell(QString::fromStdString(c.cardRef.language)));
        // Condition is optional (ungraded copies) — blank renders as an em-dash.
        table_->setItem(row, 4, cell(c.condition ? conditionAbbrev(*c.condition) : QString()));
        table_->setItem(row, 5, cell(ownershipLabel(c.ownership)));
        // Resolve the copy's binder once — its name goes in the cell, its region
        // into the search haystack below.
        const BinderInfo* binder = nullptr;
        if (c.binderId) {
            const auto it = binderInfo.find(*c.binderId);
            if (it != binderInfo.end()) {
                binder = &it->second;
            }
        }
        const QString binderName = binder ? binder->name : QString();
        auto* binderCell = cell(binderName);
        binderCell->setToolTip(binderName);
        table_->setItem(row, 6, binderCell);

        // Precompute this row's lowercased search text from its cells, plus a few
        // fields that don't show verbatim in the table yet users still expect to
        // filter by: the dex number (the "#" column was dropped), the full
        // condition label (the column abbreviates to NM/LP/…), and both regions —
        // the species' region and, if filed, the binder's region.
        QString hay = QString::number(c.pokemonDexNum) + QLatin1Char(' ');
        for (int col = 0; col < table_->columnCount(); ++col) {
            hay += table_->item(row, col)->text() + QLatin1Char(' ');
        }
        if (c.condition) {
            hay += conditionLabel(*c.condition) + QLatin1Char(' ');
        }
        hay += speciesRegionLabel(c.pokemonDexNum) + QLatin1Char(' ');
        if (binder && !binder->region.isEmpty()) {
            hay += binder->region + QLatin1Char(' ');
        }
        haystacks_[row] = hay.toLower();
    }
    // Size the free-text columns (Set, Binder) to their content, then cap them: a
    // long set or user-named binder elides (…) — with the full value on its tooltip
    // and the column still draggable — rather than crowding out the flexible Pokémon
    // column. The cap is a representative long set name, so ordinary names show whole.
    const int freeTextCap =
        table_->fontMetrics().horizontalAdvance(QStringLiteral("McDonald's Collection 2021")) + 32;
    for (const int col : {2, 6}) {
        table_->resizeColumnToContents(col);
        if (table_->columnWidth(col) > freeTextCap) {
            table_->setColumnWidth(col, freeTextCap);
        }
    }
    // Empty state: swap the table + search + row actions for a friendly hint when
    // nothing is stored.
    const bool empty = loaded_.empty();
    table_->setVisible(!empty);
    search_->setVisible(!empty);
    countLabel_->setVisible(!empty);
    assignButton_->setVisible(!empty);
    removeButton_->setVisible(!empty);
    emptyLabel_->setVisible(empty);

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
