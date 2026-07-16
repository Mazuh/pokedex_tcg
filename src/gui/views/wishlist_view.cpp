#include "gui/views/wishlist_view.h"

#include <QComboBox>
#include <QCompleter>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
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
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <exception>
#include <optional>
#include <string>
#include <utility>

#include "core/app/wishlist_service.h"
#include "core/domain/pokemon.h"
#include "core/domain/pokemon_catalog.h"
#include "gui/views/source_label.h"
#include "gui/views/table_cell.h"

namespace pokedex {

namespace {

// Roles under which the column-0 cell stashes the row's target — the Pokémon and
// the exact source string — so Edit/Delete act on the true values regardless of
// how the Source column is rendered (a plain cell or a clickable link widget).
constexpr int kDexRole = Qt::UserRole;
constexpr int kSourceRole = Qt::UserRole + 1;

// Add… dialog: pick a species (type-to-search over the whole catalog) and type a
// source. Returns the chosen dex number and source text, or nullopt on cancel.
std::optional<std::pair<int, QString>> promptAddSource(QWidget* parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle(QObject::tr("Add to Wishlist"));

    auto* species = new QComboBox(&dialog);
    species->setEditable(true);
    species->setInsertPolicy(QComboBox::NoInsert);  // type to filter, not to add
    species->completer()->setCompletionMode(QCompleter::PopupCompletion);
    for (const Pokemon& pokemon : pokemonCatalog()) {
        species->addItem(
            QStringLiteral("#%1 %2").arg(pokemon.dexNumber).arg(
                QString::fromStdString(pokemon.name)),
            pokemon.dexNumber);
    }

    auto* source = new QLineEdit(&dialog);
    source->setPlaceholderText(QObject::tr("Seller or link…"));

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto* form = new QFormLayout(&dialog);
    form->addRow(QObject::tr("Pokémon:"), species);
    form->addRow(QObject::tr("Source:"), source);
    form->addRow(buttons);

    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    // Resolve the species from the combo's *text*, not currentIndex(): an editable
    // combo's index does not track freeform typing, so reading the index could
    // attach the source to whatever was last highlighted (e.g. item 0). Match the
    // text back to a real item; if it doesn't (a partial the user never picked
    // from the popup), treat it as no selection rather than guess.
    const int index = species->findText(species->currentText(), Qt::MatchFixedString);
    if (index < 0) {
        return std::nullopt;
    }
    const int dex = species->itemData(index).toInt();
    return std::make_pair(dex, source->text());
}

}  // namespace

WishlistView::WishlistView(WishlistService& wishlist, QWidget* parent)
    : QWidget(parent), wishlist_(wishlist) {
    // A read-only three-column table: dex number, name, source (one row per
    // source). Whole-row single selection; the source column takes the slack.
    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon"), tr("Source")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The "#" header sits over right-aligned dex numbers, so right-align it to match.
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

    // Shown in place of the table when the wishlist is empty.
    emptyLabel_ = new QLabel(
        tr("Your wishlist is empty. Add sellers or links from a Pokémon's page, "
           "or with “Add…”."),
        this);
    emptyLabel_->setWordWrap(true);
    emptyLabel_->setEnabled(false);  // muted: a hint, not content

    auto* addButton = new QPushButton(tr("Add…"), this);
    editButton_ = new QPushButton(tr("Edit…"), this);
    removeButton_ = new QPushButton(tr("Delete"), this);
    removeButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));

    connect(addButton, &QPushButton::clicked, this, &WishlistView::addEntry);
    connect(editButton_, &QPushButton::clicked, this, &WishlistView::editSelected);
    connect(removeButton_, &QPushButton::clicked, this, &WishlistView::removeSelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this,
            &WishlistView::updateButtonState);
    // Double-click a row to edit it, the usual list-activation gesture.
    connect(table_, &QTableWidget::cellActivated, this, &WishlistView::editSelected);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(addButton);
    buttons->addWidget(editButton_);
    buttons->addWidget(removeButton_);
    buttons->addStretch();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    layout->addWidget(table_);
    layout->addWidget(emptyLabel_);
    layout->addLayout(buttons);

    refresh();
}

void WishlistView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();
}

void WishlistView::refresh() {
    table_->setRowCount(0);
    int row = 0;
    try {
        for (const WishlistEntry& entry : wishlist_.listAll()) {
            const QString name = QString::fromStdString(entry.pokemon.name);
            for (const std::string& source : entry.sources) {
                const QString text = QString::fromStdString(source);
                table_->insertRow(row);

                auto* number = cell(QString::number(entry.pokemon.dexNumber));
                number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                number->setData(kDexRole, entry.pokemon.dexNumber);
                number->setData(kSourceRole, text);
                table_->setItem(row, 0, number);
                table_->setItem(row, 1, cell(name));
                // A link source gets a clickable label widget; a plain seller name
                // is a normal (selectable) cell.
                if (isLinkSource(text)) {
                    table_->setCellWidget(row, 2, sourceLabel(text));
                } else {
                    table_->setItem(row, 2, cell(text));
                }
                ++row;
            }
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not load your wishlist:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }

    const bool empty = row == 0;
    table_->setVisible(!empty);
    emptyLabel_->setVisible(empty);
    updateButtonState();
}

void WishlistView::addEntry() {
    const std::optional<std::pair<int, QString>> chosen = promptAddSource(this);
    if (!chosen) {
        return;
    }
    try {
        wishlist_.addSource(chosen->first, chosen->second.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"), QString::fromUtf8(e.what()));
    }
    refresh();
}

void WishlistView::editSelected() {
    const int row = table_->currentRow();
    if (row < 0 || table_->selectedItems().isEmpty()) {
        return;
    }
    QTableWidgetItem* item = table_->item(row, 0);
    if (!item) {
        return;
    }
    const int dex = item->data(kDexRole).toInt();
    const QString oldSource = item->data(kSourceRole).toString();

    bool ok = false;
    const QString entered = QInputDialog::getText(this, tr("Edit Source"),
                                                  tr("Seller or link:"), QLineEdit::Normal,
                                                  oldSource, &ok);
    if (!ok) {
        return;
    }
    try {
        wishlist_.editSource(dex, oldSource.toStdString(), entered.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"), QString::fromUtf8(e.what()));
    }
    refresh();
}

void WishlistView::removeSelected() {
    const int row = table_->currentRow();
    if (row < 0 || table_->selectedItems().isEmpty()) {
        return;
    }
    QTableWidgetItem* item = table_->item(row, 0);
    if (!item) {
        return;
    }
    const int dex = item->data(kDexRole).toInt();
    const std::string source = item->data(kSourceRole).toString().toStdString();
    try {
        wishlist_.removeSource(dex, source);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"), QString::fromUtf8(e.what()));
    }
    refresh();
}

void WishlistView::updateButtonState() {
    const bool hasSelection =
        table_->currentRow() >= 0 && !table_->selectedItems().isEmpty();
    editButton_->setEnabled(hasSelection);
    removeButton_->setEnabled(hasSelection);
}

}  // namespace pokedex
