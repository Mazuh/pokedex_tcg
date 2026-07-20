#include "gui/views/pokemon_list_view.h"

#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QShowEvent>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <string>

#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "gui/views/add_card_copy_page.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/edit_card_copy_page.h"
#include "gui/views/owned_copy_buckets.h"
#include "gui/views/pokemon_detail_panel.h"
#include "gui/views/region_labels.h"
#include "gui/views/select_all_line_edit.h"
#include "gui/views/splitter_style.h"
#include "gui/views/table_cell.h"

namespace pokedex {

namespace {

// How many rows to append per load. The catalog is ~1000 entries; a chunk keeps
// the initial render cheap and each append unnoticeable.
constexpr int kChunkSize = 20;

// Start loading the next chunk this many pixels before the scrollbar bottom, so
// more rows are ready before the user actually hits the end. The table scrolls
// per pixel (set below), so the scrollbar range — and this margin — are pixels.
constexpr int kPrefetchMargin = 64;

}  // namespace

PokemonListView::PokemonListView(PokemonBrowseService& service, WishlistService& wishlist,
                                 MediaService& media, CardSearchService& cardSearch,
                                 CardCopyService& cardCopies, CardImageStore& cardImages,
                                 BinderService& binders, QWidget* parent)
    : QWidget(parent),
      service_(service),
      cardSearch_(cardSearch),
      cardCopies_(cardCopies),
      cardImages_(cardImages),
      binders_(binders) {
    search_ = new SelectAllLineEdit(this);
    search_->setPlaceholderText(tr("Search Pokémon or region…"));
    search_->setClearButtonEnabled(true);

    // A read-only four-column table: dex number, name, region, owned count. Whole-
    // row selection, no editing; the name column takes the slack.
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon"), tr("Region"), tr("Owned")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // "#" and "Owned" sit over right-aligned numbers, so right-align them to match.
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->horizontalHeaderItem(3)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    // Scroll per pixel so the scrollbar range is in pixels — smoother, and it
    // makes kPrefetchMargin (pixels) meaningful for the near-bottom test below.
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Cell padding so content doesn't hug the edges — and, crucially, so the
    // right-aligned Owned column clears the macOS overlay scrollbar that fades
    // in over the right edge while scrolling.
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

    countLabel_ = new QLabel(this);
    countLabel_->setEnabled(false);  // muted: a status detail, not an action

    // Copy mode is on here too (a CardImageStore is passed): a selected species that
    // owns copies shows one, so the double-click shortcut can offer to edit it. The
    // copies are aggregated across every binder (loadOwnedCopies), so tell the panel to
    // drop the counter's binder-scoped "filed here" wording.
    detail_ = new PokemonDetailPanel(media, wishlist, &cardImages_, this);
    detail_->setCountedAcrossBinders(true);

    connect(search_, &QLineEdit::textChanged, this, &PokemonListView::applyFilter);
    // Show the current row's Pokémon in the detail panel. currentCellChanged
    // covers both a mouse click (which moves the current cell) and keyboard arrow
    // navigation, so one connection suffices — connecting cellClicked too would
    // just fire showRow twice per click. Row → data is read from the cells,
    // sidestepping the filtered_ map.
    connect(table_, &QTableWidget::currentCellChanged, this, &PokemonListView::showRow);
    // Double-click / Enter on a row is the confirm-then-act shortcut. cellActivated
    // is exactly that gesture (and never fires on plain selection), so it won't race
    // showRow — by the time it fires, the row is selected and a copy is on screen.
    connect(table_, &QTableWidget::cellActivated, this,
            [this](int row, int) { activateRow(row); });
    // The detail panel's "Add copy" relays up to an in-place page push.
    connect(detail_, &PokemonDetailPanel::addCopyRequested, this, &PokemonListView::openAddCopy);
    // In copy mode, "Edit card" relays up to an in-place edit-page push.
    connect(detail_, &PokemonDetailPanel::editCopyRequested, this, &PokemonListView::openEditCopy);
    // Infinite scroll: append the next chunk as the user nears the bottom. The
    // complementary "viewport isn't full yet" case (first show, or the window
    // grew taller than the loaded rows, where no scrollbar exists) is handled by
    // eventFilter() watching the viewport's resize.
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar* bar = table_->verticalScrollBar();
        if (value >= bar->maximum() - kPrefetchMargin &&
            loadedCount_ < static_cast<int>(filtered_.size())) {
            loadMore();
        }
    });
    table_->viewport()->installEventFilter(this);

    // The list (search + table + count) on the left, the detail panel on the
    // right, in a draggable horizontal split. The list takes the slack.
    auto* listPane = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(16, 12, 16, 12);  // don't hug the section edges
    listLayout->addWidget(search_);
    listLayout->addWidget(table_);
    listLayout->addWidget(countLabel_);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(listPane);
    splitter->addWidget(detail_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({560, 240});
    thinDivider(splitter);

    // Page 0 of an inner stack is the browse splitter; "Add copy" pushes an
    // AddCardCopyPage as page 1 (the BindersPage list ⇄ detail idiom).
    stack_ = new QStackedWidget(this);
    stack_->addWidget(splitter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    // Compute the whole catalog once; filtering and lazy loading only work the
    // cached vector, never re-query. applyFilter() seeds filtered_ and loads
    // enough to fill the viewport.
    entries_ = service_.listAll();
    loadOwnedCopies();
    applyFilter();
}

bool PokemonListView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == table_->viewport() && event->type() == QEvent::Resize) {
        fillViewport();
    }
    return QWidget::eventFilter(watched, event);
}

void PokemonListView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    refresh();  // reflect copies added/edited in another section since this was last shown
}

void PokemonListView::applyFilter() {
    const QString filter = search_->text();
    filtered_.clear();
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const PokemonBrowseEntry& entry = entries_[i];
        const QString name = QString::fromStdString(entry.pokemon.name);
        const QString number = QString::number(entry.pokemon.dexNumber);
        const QString region = regionLabel(entry.pokemon.region);
        const bool visible = filter.isEmpty() ||
                             name.contains(filter, Qt::CaseInsensitive) ||
                             number.contains(filter) ||
                             region.contains(filter, Qt::CaseInsensitive);
        if (visible) {
            filtered_.push_back(i);
        }
    }
    // Reset to the top and render enough to fill the viewport again. Always
    // refresh the label afterwards so an empty (no-match) result still shows
    // "Showing 0 of 0" rather than the previous filter's stale count.
    loadedCount_ = 0;
    table_->setRowCount(0);
    table_->scrollToTop();
    fillViewport();
    updateCountLabel();
    // If the Pokémon shown in the detail panel is no longer among the results
    // (an empty result, or a narrower filter that excludes it), clear the panel
    // so it never displays a species that has no row on screen.
    const bool shownStillVisible = std::any_of(
        filtered_.begin(), filtered_.end(),
        [this](int i) { return entries_[i].pokemon.dexNumber == shownDex_; });
    if (!shownStillVisible) {
        detail_->clear();
        shownDex_ = -1;
    }
}

void PokemonListView::showRow(int row) {
    if (row < 0) {
        return;
    }
    QTableWidgetItem* number = table_->item(row, 0);
    QTableWidgetItem* name = table_->item(row, 1);
    if (!number || !name) {
        return;
    }
    shownDex_ = number->text().toInt();
    const auto it = owned_.find(shownDex_);
    if (it != owned_.end() && !it->second.empty()) {
        detail_->showPokemon(shownDex_, name->text(), it->second);
    } else {
        detail_->showPokemon(shownDex_, name->text());
    }
}

void PokemonListView::activateRow(int row) {
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

    const auto it = owned_.find(dex);
    if (it == owned_.end() || it->second.empty()) {
        // No owned card: confirm opening the add-copy page for this species.
        const auto choice = QMessageBox::question(
            this, tr("Add a card"),
            tr("%1 has no cards in your collection yet.\nAdd one now?").arg(species),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
        if (choice == QMessageBox::Yes) {
            openAddCopy(dex, species);
        }
        return;
    }

    // Owned: confirm editing the copy the detail panel is showing. Selection precedes
    // activation, so showRow has already put one of this species' copies on screen.
    const QString copyId = detail_->shownCopyId();
    if (copyId.isEmpty()) {
        return;  // defensive: nothing shown to edit
    }
    const auto choice = QMessageBox::question(
        this, tr("Edit card"), tr("Edit the shown card of %1?").arg(species),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
    if (choice == QMessageBox::Yes) {
        openEditCopy(copyId);
    }
}

void PokemonListView::openAddCopy(int dexNumber, const QString& name) {
    // Unscoped browse: the binder picker is a free choice defaulting to "— None —".
    auto* page =
        new AddCardCopyPage(cardSearch_, cardCopies_, binders_, cardImages_, dexNumber, name);
    // A newly added copy changes the Owned column; refresh so it's current.
    connect(page, &AddCardCopyPage::copyAdded, this, &PokemonListView::refresh);
    connect(page, &AddCardCopyPage::backRequested, this, [this, page]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

void PokemonListView::openEditCopy(const QString& copyId) {
    // Find the copy the detail panel is showing among this species' owned copies.
    const auto it = owned_.find(shownDex_);
    if (it == owned_.end()) {
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
        // Capture the shown species before refresh(), which re-renders from the top.
        const int dex = shownDex_;
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
        // An edit can change owned data (a comment, a new image, a binder move). Re-read
        // the inventory, re-select the edited species' row, and re-show the SAME copy —
        // not a fresh random pick — so the highlight and panel agree and the user sees
        // their change land. refresh() reset the list to the top and cleared the
        // selection; the row can also sit past the first loaded chunk, so load rows until
        // it exists before selecting (mirroring BinderView, which needs no load — all its
        // rows are always present).
        refresh();
        int targetRow = -1;
        for (int pos = 0; pos < static_cast<int>(filtered_.size()); ++pos) {
            if (entries_[filtered_[pos]].pokemon.dexNumber == dex) {
                targetRow = pos;
                break;
            }
        }
        if (targetRow < 0) {  // the species is filtered out by the active search
            detail_->clear();
            shownDex_ = -1;
            return;
        }
        while (loadedCount_ <= targetRow) {
            const int before = loadedCount_;
            loadMore();
            if (loadedCount_ == before) {
                break;  // safety: never spin if a load made no progress
            }
        }
        shownDex_ = dex;
        // setCurrentCell would re-fire showRow (a random re-roll); block it and drive the
        // panel ourselves with the just-edited copy.
        table_->blockSignals(true);
        table_->setCurrentCell(targetRow, 1);
        table_->blockSignals(false);
        if (QTableWidgetItem* item = table_->item(targetRow, 1)) {
            table_->scrollToItem(item);
        }
        const QString name = QString::fromStdString(entries_[filtered_[targetRow]].pokemon.name);
        const auto owner = owned_.find(dex);
        if (owner != owned_.end() && !owner->second.empty()) {
            detail_->showPokemon(dex, name, owner->second, copyId);
        } else {  // its only copy moved/was removed → plain artwork under the same row
            detail_->showPokemon(dex, name);
        }
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

void PokemonListView::refresh() {
    // Re-query the catalog + owned counts; applyFilter() re-renders from the top,
    // preserving the current search text (it reads search_->text()).
    entries_ = service_.listAll();
    loadOwnedCopies();
    applyFilter();
}

void PokemonListView::loadOwnedCopies() {
    // Bucket every owned, species-tied copy by dex so showRow() can hand the detail
    // panel a species' copies (copy mode). Unscoped by binder — this is the whole
    // Pokédex browser — so it reads the full inventory (listAll), unlike the binder
    // guide's scoped listByBinder. bucketOwnedCopiesByDex applies the shared Owned,
    // non-species-free predicate, so the Owned column and the double-click branch agree.
    owned_.clear();
    try {
        owned_ = bucketOwnedCopiesByDex(cardCopies_.listAll());
    } catch (const std::exception&) {
        owned_.clear();  // best-effort: fall back to artwork-only if the read fails
    }
}

void PokemonListView::loadMore() {
    const int total = static_cast<int>(filtered_.size());
    const int next = std::min(loadedCount_ + kChunkSize, total);
    if (next == loadedCount_) {
        return;  // nothing left to load
    }

    // Grow the table and fill only the newly added rows — the rows already shown
    // are left untouched, so scrolling never re-renders what's on screen.
    table_->setRowCount(next);
    for (int row = loadedCount_; row < next; ++row) {
        const PokemonBrowseEntry& entry = entries_[filtered_[row]];
        auto* number = cell(QString::number(entry.pokemon.dexNumber));
        number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 0, number);
        table_->setItem(row, 1, cell(QString::fromStdString(entry.pokemon.name)));
        table_->setItem(row, 2, cell(regionLabel(entry.pokemon.region)));
        // Render a zero owned-count as an em-dash (cell() does this for empty text)
        // so the column reads as "none" at a glance rather than a wall of 0s.
        auto* owned =
            cell(entry.ownedCount == 0 ? QString() : QString::number(entry.ownedCount));
        owned->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 3, owned);
    }
    loadedCount_ = next;
    updateCountLabel();
}

void PokemonListView::fillViewport() {
    if (filling_) {
        return;  // a nested resize (scrollbar appearing) fired mid-fill; ignore it
    }
    filling_ = true;
    const int total = static_cast<int>(filtered_.size());
    // Rows needed to overflow the viewport (so a scrollbar appears and scrolling
    // takes over the loading). Uses the default row height, which is valid even
    // before any row exists and while the vertical header is hidden.
    const int rowHeight = std::max(1, table_->verticalHeader()->defaultSectionSize());
    const int needed = table_->viewport()->height() / rowHeight + 2;
    while (loadedCount_ < total && loadedCount_ < needed) {
        const int before = loadedCount_;
        loadMore();
        if (loadedCount_ == before) {
            break;  // safety: never spin if a load made no progress
        }
    }
    filling_ = false;
}

void PokemonListView::updateCountLabel() {
    countLabel_->setText(
        tr("Showing %1 of %2 Pokémon").arg(loadedCount_).arg(static_cast<int>(filtered_.size())));
}

}  // namespace pokedex
