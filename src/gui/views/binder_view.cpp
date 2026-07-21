#include "gui/views/binder_view.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <exception>

#include "core/app/binder_guide_service.h"
#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "gui/services/card_image_store.h"
#include "gui/views/add_card_copy_page.h"
#include "gui/views/back_button.h"
#include "gui/views/binder_combo.h"
#include "gui/views/copy_row_activation.h"
#include "gui/views/edit_copy_page_host.h"
#include "gui/views/owned_copy_buckets.h"
#include "gui/views/pokemon_detail_panel.h"
#include "gui/views/select_all_line_edit.h"
#include "gui/views/sortable_table.h"
#include "gui/views/splitter_style.h"
#include "gui/views/status_labels.h"
#include "gui/views/table_cell.h"

namespace pokedex {

BinderView::BinderView(BinderGuideService& guide, const CardBinder& binder,
                       WishlistService& wishlist, MediaService& media,
                       CardSearchService& cardSearch, CardCopyService& cardCopies,
                       CardImageStore& cardImages, BinderService& binders, QWidget* parent)
    : QWidget(parent),
      guide_(guide),
      binder_(binder),
      cardSearch_(cardSearch),
      cardCopies_(cardCopies),
      cardImages_(cardImages),
      binders_(binders) {
    auto* backButton = makeBackButton(this);
    auto* heading = new QLabel(binderComboLabel(binder), this);

    connect(backButton, &QPushButton::clicked, this, &BinderView::backRequested);

    // A top bar: Back on the left, the binder's name beside it.
    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    search_ = new SelectAllLineEdit(this);
    search_->setPlaceholderText(tr("Search Pokémon…"));
    search_->setClearButtonEnabled(true);

    // A read-only three-column table: dex number, name, status. Whole-row
    // selection, no editing; the Pokémon column takes the slack.
    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon"), tr("Status")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The "#" header sits over right-aligned dex numbers, so right-align it to match.
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    // Cell padding so content clears the edges and the overlay scrollbar.
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

    detail_ = new PokemonDetailPanel(media, wishlist, &cardImages_, this);

    connect(search_, &QLineEdit::textChanged, this, &BinderView::applyFilter);
    // Show the current row's Pokémon in the detail panel. currentCellChanged
    // covers both a mouse click (which moves the current cell) and keyboard arrow
    // navigation, so one connection suffices — connecting cellClicked too would
    // just fire showRow twice per click. (cellActivated would be double-click/Enter
    // — the wrong gesture.)
    connect(table_, &QTableWidget::currentCellChanged, this, &BinderView::showRow);
    // Double-click / Enter is the confirm-then-act shortcut (edit the shown copy, or add
    // one if none is filed here). cellActivated is exactly that gesture and never fires on
    // plain selection, so it won't race showRow — by the time it fires the row is selected
    // and a copy is on screen. Mirrors the Pokémon browser and My Cards.
    connect(table_, &QTableWidget::cellActivated, this,
            [this](int row, int) { activateRow(row); });
    // Clicking a header sorts the guide by that column; store the choice and
    // repopulate from the cached entries_ (re-sorted so they stay 1:1 with the rows).
    // A pure reorder — it never recomputes the guide or re-reads the binder's copies.
    installHeaderSort(table_, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        sortOrder_ = order;
        repopulate();
    });
    // The detail panel's "Add copy" relays up to an in-place page push.
    connect(detail_, &PokemonDetailPanel::addCopyRequested, this, &BinderView::openAddCopy);
    // In copy mode, "Edit card" relays up to an in-place edit-page push.
    connect(detail_, &PokemonDetailPanel::editCopyRequested, this, &BinderView::openEditCopy);

    // The list (top bar + search + table) on the left, the detail panel on the
    // right, in a draggable horizontal split. The list takes the slack.
    auto* listPane = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    listLayout->addLayout(topBar);
    listLayout->addWidget(search_);
    listLayout->addWidget(table_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(listPane);
    splitter->addWidget(detail_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({560, 240});
    thinDivider(splitter);

    // Page 0 of an inner stack is the guide splitter; "Add copy" pushes an
    // AddCardCopyPage as page 1 and Back returns here.
    stack_ = new QStackedWidget(this);
    stack_->addWidget(splitter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    refresh();  // compute the guide and populate the table for the first time
}

void BinderView::refresh() {
    // (Re)compute the guide's entries. A failure here (e.g. the workspace went
    // away) is reported and leaves an empty table rather than crashing.
    try {
        entries_ = guide_.buildEntries(binder_);
    } catch (const std::exception& e) {
        entries_.clear();
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not open this binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }

    // Bucket the binder's owned copies by species so showRow() can hand the detail
    // panel the copies to display (copy mode). bucketOwnedCopiesByDex applies the shared
    // "owned + species-tied" predicate (mirroring the "Completed" status). Scoped read
    // (listByBinder), not a whole-inventory scan — unlike the Pokémon browser's listAll.
    ownedHere_.clear();
    try {
        ownedHere_ = bucketOwnedCopiesByDex(cardCopies_.listByBinder(binder_.id));
    } catch (const std::exception&) {
        ownedHere_.clear();  // best-effort: fall back to artwork-only if the read fails
    }

    repopulate();
}

void BinderView::repopulate() {
    // Remember which species + copy the detail panel is showing before the rebuild.
    // A header-sort reorders the rows, so the selection must be restored by IDENTITY
    // (the species' dex number), not by the old row index — restoring by row index
    // would leave a different species highlighted and the panel + "Edit card…"
    // targeting the wrong one. shownDex_ is the shown species; "" / -1 when nothing
    // is shown (no selection).
    const QString shownCopyBefore = detail_->shownCopyId();
    const int selectedDex = table_->selectedItems().isEmpty() ? -1 : shownDex_;

    sortEntries();

    // Rebuild every row from the freshly computed entries; entries_ and table rows
    // stay 1:1 and aligned. Statuses are fixed until the next refresh, so filtering
    // then just toggles row visibility — no per-keystroke allocation.
    table_->setRowCount(static_cast<int>(entries_.size()));
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CardBinderEntry& entry = entries_[i];
        auto* number = cell(QString::number(entry.pokemon.dexNumber));
        number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(i, 0, number);
        table_->setItem(i, 1, cell(QString::fromStdString(entry.pokemon.name)));
        table_->setItem(i, 2, cell(statusLabel(entry.status)));
    }

    // Move the highlight to the row the selected species landed on after the sort, so
    // the selection follows the record rather than the row index. Block signals so
    // setCurrentCell doesn't re-fire showRow (which re-rolls a random copy); the panel
    // is re-driven explicitly below with the SAME copy that was on screen.
    if (selectedDex >= 0) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (entries_[i].pokemon.dexNumber == selectedDex) {
                table_->blockSignals(true);
                table_->setCurrentCell(i, 1);
                table_->blockSignals(false);
                break;
            }
        }
    }

    applyFilter(search_->text());  // preserve the current filter across a refresh

    // Re-drive the panel from the (identity-restored) current row so the highlight and
    // panel stay in agreement, mirroring OwnedCardsView. Re-show the SAME copy that was
    // on screen (shownCopyBefore) rather than calling showRow(), which re-rolls a random
    // copy of the species. openEditCopy re-selects the just-edited copy after its own
    // refresh(), which overrides this.
    const int current = table_->currentRow();
    if (current >= 0 && !table_->isRowHidden(current) && !table_->selectedItems().isEmpty()) {
        QTableWidgetItem* number = table_->item(current, 0);
        QTableWidgetItem* name = table_->item(current, 1);
        if (number && name) {
            shownDex_ = number->text().toInt();
            showSpeciesInPanel(shownDex_, name->text(), shownCopyBefore);
        }
    }
}

void BinderView::sortEntries() {
    // The name column allocates a QString, so precompute each row's keys once (via
    // sortByKeys) rather than rebuilding them for both operands on every comparison.
    // A sortColumn_ < 0 keeps the guide's natural (dex) order.
    struct Key {
        int dexNumber;
        QString name;
        int statusRank;
    };
    sortByKeys(
        entries_, sortColumn_, sortOrder_,
        [](const CardBinderEntry& e) {
            return Key{e.pokemon.dexNumber, QString::fromStdString(e.pokemon.name),
                       static_cast<int>(e.status)};
        },
        [](const Key& a, const Key& b, int column) -> int {
            switch (column) {
                case 0:
                    return compareValues(a.dexNumber, b.dexNumber);
                case 1:
                    return a.name.localeAwareCompare(b.name);
                case 2:
                    // CollectionStatus enum values are the documented precedence order —
                    // a more meaningful grouping than the status labels' alphabetical order.
                    return compareValues(a.statusRank, b.statusRank);
            }
            return 0;
        });
}

void BinderView::applyFilter(const QString& filter) {
    bool shownStillVisible = false;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const QString name = QString::fromStdString(entries_[i].pokemon.name);
        const bool visible = filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive);
        table_->setRowHidden(i, !visible);
        if (visible && entries_[i].pokemon.dexNumber == shownDex_) {
            shownStillVisible = true;
        }
    }
    // If the Pokémon shown in the detail panel is now hidden (an empty result, or
    // a filter that excludes it), clear the panel so it never shows a species with
    // no visible row.
    if (!shownStillVisible) {
        detail_->clear();
        shownDex_ = -1;
    }
}

void BinderView::showRow(int row) {
    if (row < 0) {
        return;
    }
    QTableWidgetItem* number = table_->item(row, 0);
    QTableWidgetItem* name = table_->item(row, 1);
    if (!number || !name) {
        return;
    }
    shownDex_ = number->text().toInt();
    showSpeciesInPanel(shownDex_, name->text());
}

void BinderView::showSpeciesInPanel(int dex, const QString& name, const QString& preferCopyId) {
    const auto it = ownedHere_.find(dex);
    if (it != ownedHere_.end()) {
        detail_->showPokemon(dex, name, it->second, preferCopyId);
    } else {
        detail_->showPokemon(dex, name);
    }
}

void BinderView::activateRow(int row) {
    if (row < 0) {
        return;
    }
    QTableWidgetItem* number = table_->item(row, 0);
    QTableWidgetItem* name = table_->item(row, 1);
    if (!number || !name) {
        return;
    }
    const int dex = number->text().toInt();
    const QString species = name->text();

    const auto it = ownedHere_.find(dex);
    const bool ownedHere = it != ownedHere_.end() && !it->second.empty();
    const QString copyId = detail_->shownCopyId();
    activateCopyRow(
        {this, ownedHere, species, copyId,
         tr("%1 has no cards filed in this binder yet.\nAdd one now?").arg(species),
         [this, dex, species]() { openAddCopy(dex, species); },
         [this, copyId]() { openEditCopy(copyId); }});
}

void BinderView::openAddCopy(int dexNumber, const QString& name) {
    // Scoped to this binder: the copy is filed here and the picker is locked to it.
    auto* page =
        new AddCardCopyPage(cardSearch_, cardCopies_, binders_, cardImages_, dexNumber, name,
                            binder_.id);
    // Adding a copy recomputes the guide: the copy is filed in this binder (so it
    // becomes "Completed" here) and submit auto-returns, so refresh so the guide
    // isn't stale on the way back.
    connect(page, &AddCardCopyPage::copyAdded, this, &BinderView::refresh);
    connect(page, &AddCardCopyPage::backRequested, this, [this, page]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

void BinderView::openEditCopy(const QString& copyId) {
    // Find the copy the detail panel is showing among this species' owned copies.
    const CardCopy* copy = findOwnedCopy(ownedHere_, shownDex_, copyId);
    if (!copy) {
        return;
    }
    pushEditCopyPage(stack_, cardSearch_, cardImages_, cardCopies_, *copy, binders_.list(),
                     [this, copyId]() {
                         // Capture the shown species before refresh(), which may clear
                         // shownDex_. An edit can change the guide (a comment, a binder
                         // move that removes the copy from here, a new image), so
                         // recompute, then re-show the SAME copy — not a fresh random pick.
                         const int dex = shownDex_;
                         refresh();
                         reselectSpecies(dex, copyId);
                     });
}

void BinderView::reselectSpecies(int dex, const QString& copyId) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (entries_[i].pokemon.dexNumber == dex) {
            table_->blockSignals(true);  // setCurrentCell would re-fire showRow (random)
            table_->setCurrentCell(i, 1);
            table_->blockSignals(false);
            shownDex_ = dex;
            // If the copy left the binder (moved/removed), showSpeciesInPanel falls back
            // to plain artwork.
            showSpeciesInPanel(dex, QString::fromStdString(entries_[i].pokemon.name), copyId);
            return;
        }
    }
    // The species left the guide entirely (its only copy moved away).
    detail_->clear();
    shownDex_ = -1;
}

}  // namespace pokedex
