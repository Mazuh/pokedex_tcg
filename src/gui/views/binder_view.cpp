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
#include "gui/views/add_card_copy_page.h"
#include "gui/views/back_button.h"
#include "gui/views/binder_combo.h"
#include "gui/views/pokemon_detail_panel.h"
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

    search_ = new QLineEdit(this);
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

    detail_ = new PokemonDetailPanel(media, wishlist, this);

    connect(search_, &QLineEdit::textChanged, this, &BinderView::applyFilter);
    // Show the current row's Pokémon in the detail panel. currentCellChanged
    // covers both a mouse click (which moves the current cell) and keyboard arrow
    // navigation, so one connection suffices — connecting cellClicked too would
    // just fire showRow twice per click. (cellActivated would be double-click/Enter
    // — the wrong gesture.)
    connect(table_, &QTableWidget::currentCellChanged, this, &BinderView::showRow);
    // The detail panel's "Add copy" relays up to an in-place page push.
    connect(detail_, &PokemonDetailPanel::addCopyRequested, this, &BinderView::openAddCopy);

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
    detail_->showPokemon(shownDex_, name->text());
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

}  // namespace pokedex
