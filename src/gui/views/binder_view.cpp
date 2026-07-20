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

#include <algorithm>
#include <exception>
#include <string>

#include "core/app/binder_guide_service.h"
#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "gui/services/card_image_store.h"
#include "gui/views/add_card_copy_page.h"
#include "gui/views/back_button.h"
#include "gui/views/binder_combo.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/edit_card_copy_page.h"
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
    // Clicking a header sorts the guide by that column; store the choice and
    // rebuild the rows (entries_ is re-sorted so it stays 1:1 with the rows).
    installHeaderSort(table_, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        sortOrder_ = order;
        refresh();
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
    // Remember which copy the detail panel is showing before the rebuild, so the
    // tail below can re-show that exact copy rather than let showPokemon re-roll a
    // random one. "" when not in copy mode.
    const QString shownCopyBefore = detail_->shownCopyId();

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
    // panel the copies to display (copy mode). Only owned copies tied to a species
    // qualify — that mirrors the "Completed" status. Scoped read (listByBinder), not a
    // whole-inventory scan.
    ownedHere_.clear();
    try {
        for (const CardCopy& copy : cardCopies_.listByBinder(binder_.id)) {
            if (copy.pokemonDexNum && copy.ownership == CardOwnership::Owned) {
                ownedHere_[*copy.pokemonDexNum].push_back(copy);
            }
        }
    } catch (const std::exception&) {
        ownedHere_.clear();  // best-effort: fall back to artwork-only if the read fails
    }

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
    applyFilter(search_->text());  // preserve the current filter across a refresh

    // A header-sort reorders the rows under a stationary highlight, so the selected
    // row index now holds a different species than the detail panel shows. Re-drive
    // the panel from the current row so the highlight and panel stay in agreement
    // (and "Edit card…" targets the highlighted species), mirroring OwnedCardsView.
    // Re-show the SAME copy that was on screen (shownCopyBefore) rather than calling
    // showRow(), which re-rolls a random copy of the species — a sort re-render must
    // not swap the copy the user is reading. openEditCopy re-selects the just-edited
    // copy after its own refresh(), which overrides this.
    const int current = table_->currentRow();
    if (current >= 0 && !table_->isRowHidden(current) && !table_->selectedItems().isEmpty()) {
        QTableWidgetItem* number = table_->item(current, 0);
        QTableWidgetItem* name = table_->item(current, 1);
        if (number && name) {
            shownDex_ = number->text().toInt();
            const auto it = ownedHere_.find(shownDex_);
            if (it != ownedHere_.end()) {
                detail_->showPokemon(shownDex_, name->text(), it->second, shownCopyBefore);
            } else {
                detail_->showPokemon(shownDex_, name->text());
            }
        }
    }
}

void BinderView::sortEntries() {
    applyColumnSort(entries_, sortColumn_, sortOrder_,
                    [](const CardBinderEntry& a, const CardBinderEntry& b, int column) -> int {
                        switch (column) {
                            case 0:
                                return compareValues(a.pokemon.dexNumber, b.pokemon.dexNumber);
                            case 1:
                                return QString::fromStdString(a.pokemon.name)
                                    .localeAwareCompare(QString::fromStdString(b.pokemon.name));
                            case 2:
                                // Sort by the CollectionStatus enum, whose values are the
                                // documented precedence order — a more meaningful grouping
                                // than the status labels' alphabetical order.
                                return compareValues(static_cast<int>(a.status),
                                                     static_cast<int>(b.status));
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
    const auto it = ownedHere_.find(shownDex_);
    if (it != ownedHere_.end()) {
        detail_->showPokemon(shownDex_, name->text(), it->second);
    } else {
        detail_->showPokemon(shownDex_, name->text());
    }
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
    const auto it = ownedHere_.find(shownDex_);
    if (it == ownedHere_.end()) {
        return;
    }
    const std::string id = copyId.toStdString();
    const auto copyIt = std::find_if(it->second.begin(), it->second.end(),
                                     [&](const CardCopy& c) { return c.id == id; });
    if (copyIt == it->second.end()) {
        return;
    }
    auto* page = new EditCardCopyPage(cardSearch_, cardImages_, cardCopies_, *copyIt,
                                      binders_.list(), titleFor(*copyIt));
    connect(page, &EditCardCopyPage::backRequested, this, [this, page, copyId]() {
        // Capture the shown species before refresh(), which may clear shownDex_.
        const int dex = shownDex_;
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
        // An edit can change the guide (a comment, a binder move that removes the copy
        // from here, a new image). Recompute, then re-show the SAME copy that was just
        // edited — not a fresh random pick — so the user sees their change land.
        refresh();
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (entries_[i].pokemon.dexNumber == dex) {
                table_->blockSignals(true);  // setCurrentCell would re-fire showRow (random)
                table_->setCurrentCell(i, 1);
                table_->blockSignals(false);
                shownDex_ = dex;
                const QString name = QString::fromStdString(entries_[i].pokemon.name);
                const auto it = ownedHere_.find(dex);
                if (it != ownedHere_.end()) {
                    detail_->showPokemon(dex, name, it->second, copyId);
                } else {  // the copy left the binder (moved/removed) → plain artwork
                    detail_->showPokemon(dex, name);
                }
                return;
            }
        }
        // The species left the guide entirely (its only copy moved away).
        detail_->clear();
        shownDex_ = -1;
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

}  // namespace pokedex
