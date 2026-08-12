#include "gui/views/binder_view.h"

#include <QBrush>
#include <QColor>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QModelIndex>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QShowEvent>
#include <QSplitter>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cstddef>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/app/binder_guide_service.h"
#include "core/app/binder_move_planner.h"
#include "core/app/binder_service.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_ownership.h"
#include "gui/services/card_image_store.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/add_card_copy_page.h"
#include "gui/views/back_button.h"
#include "gui/views/backable_page_host.h"
#include "gui/views/binder_combo.h"
#include "gui/views/binder_edit_page.h"
#include "gui/views/binder_layout_labels.h"
#include "gui/views/bulk_refresh_controller.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/condition_labels.h"
#include "gui/views/copy_row_activation.h"
#include "gui/views/edit_copy_page_host.h"
#include "gui/views/prices_page_host.h"
#include "gui/views/foil_labels.h"
#include "gui/views/move_card_dialog.h"
#include "gui/views/owned_copy_buckets.h"
#include "gui/views/pokemon_detail_panel.h"
#include "gui/views/price_labels.h"
#include "gui/views/rarity_labels.h"
#include "gui/views/select_all_line_edit.h"
#include "gui/views/sortable_table.h"
#include "gui/views/splitter_style.h"
#include "gui/views/status_labels.h"
#include "gui/views/table_bulk_update.h"
#include "gui/views/table_cell.h"
#include "gui/views/toast.h"
#include "gui/views/wishlist_edit_page.h"

namespace pokedex {

namespace {
// The guide's auto-fit columns: every column that sizes to its content
// (ResizeToContents) — all but the Stretch Set column (4). The single source of truth
// for both the ctor's initial resize-mode setup and repopulate()'s BulkTablePopulate
// guard, so the two can't silently drift when a column is added, removed, or reordered
// (a mismatch would either reintroduce the O(rows^2) reopen freeze or convert the Set
// slack column). Note column 3 carries a Pokémon name OR a card name (a species-free
// row has no species to name it). Mirrors OwnedCardsView's kAutoFitColumns.
constexpr int kAutoFitColumns[] = {0, 1, 2, 3, 5, 6, 7, 8, 9, 10};

// The Prices column alone — the subset updatePricesFor() rewrites in place. It needs the
// same BulkTablePopulate treatment as a full rebuild (col 10 is content-sized), but must
// not disturb the other columns' modes, since nothing else on that path changes.
constexpr int kPriceColumnOnly[] = {10};

// The guide's first two columns say where a row physically sits in the album — which Page,
// and which Pocket within it as "row×column" counting from the page's top-left ("2×3" =
// second row, third pocket across) — and a separator is drawn under the row that closes a
// page, so the list reads as the stack of pages it maps to. All of it comes from the
// binder's own recorded pocket grid (CardBinderPocketGrid); a binder that doesn't record
// one shows nothing here rather than being assumed to be 3×3.
//
// Both are keyed to the row's position in the FILED order (entries_), never to its position
// among the visible rows. That is the whole point of the page number: searching for a
// species hides most rows, and the surviving row must still name the page it is physically
// on. A visible-position number would renumber itself per search and send the user to the
// wrong sleeve. The tradeoff is that the separator lines look arbitrary while a filter is
// active (they mark filed-order boundaries, and the rows between them are hidden) — the
// page column is the part that stays meaningful there.
//
// A pocket is counted for every row EXCEPT a Removed copy's: that row stays listed and
// grayed as frozen history, but the card is not in the sleeve, so counting it would push
// every card after it a pocket further along and misreport the page for the whole rest of
// the binder. So the running count skips it and its own Page/Pocket cells are blank. Two
// row kinds that hold NO card do still take a pocket, deliberately: a placeholder row (a
// listed species with nothing filed — that sleeve is reserved for it) and a blank row (a
// pocket the user chose to leave empty, whose entire purpose is to occupy space).

// Set on the Page cell (column 0) of the row that closes a page; PageBreakDelegate reads it
// from whichever cell it is painting. Carried as item data rather than recomputed from the
// row index because the pocket count skips rows (see above), so "closes a page" is no
// longer a function of the row number alone.
constexpr int kPageEndRole = Qt::UserRole + 1;

class PageBreakDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyledItemDelegate::paint(painter, option, index);
        if (!index.sibling(index.row(), 0).data(kPageEndRole).toBool()) {
            return;
        }
        // Follow the palette (so it works in light and dark) but stay muted — this is a
        // grouping hint, not a grid.
        QColor line = option.palette.color(QPalette::Text);
        line.setAlpha(110);
        painter->save();
        painter->setPen(QPen(line, 1));
        const int y = option.rect.bottom();
        painter->drawLine(option.rect.left(), y, option.rect.right(), y);
        painter->restore();
    }
};

bool isRemoved(const CardCopy& copy) { return copy.ownership == CardOwnership::Removed; }

// A row standing for a pocket the user deliberately left empty: it names neither a
// species nor a card (see CardBinderEntry).
bool isBlankSlot(const CardBinderEntry& entry) {
    return !entry.pokemon && !entry.cardCopyId;
}

// A cell for a column that does not apply at all — distinct from cell(""), whose
// em-dash reads as "this record has no data". Used for Page/Pocket when the binder
// records no pocket grid, or while a header sort has left filed order behind.
QTableWidgetItem* emptyCell() {
    auto* item = new QTableWidgetItem;
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

// What the name column says for a row, and the string both the sort key and the search
// filter use — one helper so the three can't drift. A species row is named by its
// species (so a Pokémon's several copies read as one clean block, disambiguated by
// the Set/Collector columns); a species-free row falls back to the printed card
// name, exactly as My Cards' first column does. A blank pocket says so in words rather
// than leaving an unexplained empty row — which also makes it findable by typing "blank".
QString rowLabel(const CardBinderEntry& entry, const CardCopy* copy) {
    if (isBlankSlot(entry)) {
        return QCoreApplication::translate("pokedex", "(blank pocket)");
    }
    if (entry.pokemon) {
        return QString::fromStdString(entry.pokemon->name);
    }
    return copy ? speciesOrCardName(*copy) : QString();
}

// Whether a row survives the search box. It matches the label the row SHOWS, plus the
// filed card's own printed name — a species row is labelled by its species, so without
// the second test a search for "Dark Charizard" would find nothing even with that exact
// card filed here and its row on screen, which is not what the placeholder promises.
bool rowMatchesFilter(const CardBinderEntry& entry, const CardCopy* copy,
                      const QString& filter) {
    if (filter.isEmpty()) {
        return true;
    }
    if (rowLabel(entry, copy).contains(filter, Qt::CaseInsensitive)) {
        return true;
    }
    return copy != nullptr &&
           QString::fromStdString(copy->cardRef.name).contains(filter, Qt::CaseInsensitive);
}
}  // namespace

BinderView::BinderView(BinderGuideService& guide, const CardBinder& binder,
                       WishlistService& wishlist, MediaService& media,
                       CardSearchService& cardSearch, CardPriceLookupService& priceLookup,
                       CardCopyService& cardCopies, CardImageStore& cardImages,
                       BinderService& binders, QWidget* parent)
    : QWidget(parent),
      guide_(guide),
      binder_(binder),
      wishlist_(wishlist),
      cardSearch_(cardSearch),
      priceLookup_(priceLookup),
      cardCopies_(cardCopies),
      cardImages_(cardImages),
      binders_(binders) {
    auto* backButton = makeBackButton(this);
    heading_ = new QLabel(binderComboLabel(binder), this);

    connect(backButton, &QPushButton::clicked, this, &BinderView::backRequested);

    // "Add a card" opens the species-free add page locked to this binder. It sits in the
    // chrome rather than on a row because a Trainer/Energy card depicts no species, so
    // there is no row to start from — the panel's Add covers the species flow, which is
    // left untouched. "Edit binder" opens the binder's edit screen in place — a dedicated
    // page, not a modal (see the screens-over-modals convention). "Refresh prices" bulk
    // re-fetches every linked card filed here — a manual keep-updated action, paced so it
    // never bursts the API. A muted progress label sits beside it while it runs.
    bulkStatus_ = new QLabel(this);
    bulkStatus_->setStyleSheet(QStringLiteral("color: gray;"));
    bulkStatus_->hide();
    auto* addCardButton = new QPushButton(tr("Add a card"), this);
    addCardButton->setToolTip(
        tr("Record a card that depicts no Pokémon — a Trainer, Energy or promo card — and "
           "file it in this binder."));
    connect(addCardButton, &QPushButton::clicked, this, &BinderView::openAddCard);
    auto* editBinderButton = new QPushButton(tr("Edit binder"), this);
    editBinderButton->setToolTip(tr("Change this binder's name or physical size."));
    connect(editBinderButton, &QPushButton::clicked, this, &BinderView::openEditBinder);
    refreshPricesButton_ = new QPushButton(tr("Refresh prices"), this);
    refreshPricesButton_->setToolTip(
        tr("Re-fetch market prices for every linked card filed in this binder."));

    // A top bar: Back on the left, the binder's name beside it, Add + Edit + bulk refresh
    // on the right.
    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading_);
    topBar->addStretch();
    topBar->addWidget(bulkStatus_);
    topBar->addWidget(addCardButton);
    topBar->addWidget(editBinderButton);
    topBar->addWidget(refreshPricesButton_);

    // The bulk refresh runs on the shared price service (global cap, one bulk at a time). The
    // controller wires the button + label to it: it gathers every non-Removed linked id (the
    // same set loadCachedPrices reads, so every row that can show a price can also refresh
    // it) and, on finish, folds the results into this view with ONE rebuild.
    // Parented to this view (last arg), so it lives and dies with it — no member to hold.
    new BulkRefreshController(
        priceLookup_, refreshPricesButton_, bulkStatus_,
        [this]() {
            const auto notRemoved = [](const CardCopy& c) { return !isRemoved(c); };
            return distinctExternalIds(filedCopies_, notRemoved);
        },
        [this]() {
            loadCachedPrices();
            updateStats(filedCopies_);
            repopulate();
        },
        this);

    // A muted subtitle line under the top bar carrying the binder's stats: how many
    // species are listed, how many captured (+%), how many physical cards are filed here,
    // and their market $ value. One QLabel PER FIGURE rather than a single line, because
    // each needs its own hover tooltip and Qt has no per-span tooltip inside a rich-text
    // label. The muted colour is declared once on the container and cascades to the
    // children; each label carries its own leading " · " so hiding one takes its
    // separator with it (no dangling middot to bookkeep). Filled by updateStats().
    auto* statsRow = new QWidget(this);
    statsRow->setStyleSheet(QStringLiteral("QLabel { color: gray; }"));
    auto* statsLayout = new QHBoxLayout(statsRow);
    statsLayout->setContentsMargins(0, 0, 0, 0);
    statsLayout->setSpacing(0);
    listedStat_ = new QLabel(statsRow);
    listedStat_->setToolTip(tr("Pokémon species this binder reserves a pocket for: every "
                               "species in its regions. Cards filed here that fall outside "
                               "them are extras and don't add to this."));
    capturedStat_ = new QLabel(statsRow);
    capturedStat_->setToolTip(tr("How many of those reserved pockets hold an owned card, "
                                 "and what share of the listed species that is. A card you "
                                 "moved out of its Pokédex pocket leaves it uncollected."));
    cardsStat_ = new QLabel(statsRow);
    cardsStat_->setToolTip(tr("Card copies physically filed in this binder right now. "
                              "Duplicates count separately and Trainer/Energy cards are "
                              "included; incoming and removed cards are not."));
    valueStat_ = new QLabel(statsRow);
    valueStat_->setToolTip(tr("Market value of the owned cards filed here, from locally "
                              "cached prices — a lower bound, since a card whose price was "
                              "never fetched adds nothing. USD and EUR are totalled "
                              "separately (no currency conversion)."));
    statsLayout->addWidget(listedStat_);
    statsLayout->addWidget(capturedStat_);
    statsLayout->addWidget(cardsStat_);
    statsLayout->addWidget(valueStat_);
    statsLayout->addStretch();

    search_ = new SelectAllLineEdit(this);
    search_->setPlaceholderText(tr("Search by Pokémon or card name…"));
    search_->setClearButtonEnabled(true);

    // A read-only table: the binder page this slot falls on, dex number, name, then the
    // printed-identity columns mirroring
    // My Cards (set, collector, condition, rarity, foil) for the row's own filed copy,
    // its Status, and finally that copy's cached market Prices ("$… · €…", cache-only —
    // never a network read). Whole-row selection, no editing. The Set column takes the
    // slack (as in My Cards); the name column sizes to content so it is never truncated.
    // A placeholder row (a listed species with nothing filed here — most rows in a fresh
    // binder) leaves the copy columns blank; a species-free card's row leaves "#" blank,
    // since it has no Pokédex number.
    table_ = new QTableWidget(this);
    table_->setColumnCount(11);
    table_->setHorizontalHeaderLabels({tr("Page"), tr("Pocket"), tr("#"), tr("Pokémon / Card"),
                                       tr("Set name / expansion code"),
                                       tr("Collector"), tr("Cond."), tr("Rarity"), tr("Foil"),
                                       tr("Status"), tr("Prices")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // "Page" and "#" sit over right-aligned numbers, so right-align them to match.
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Both tooltips are re-set per refresh (the grid can change under Edit binder) —
    // see updatePocketHeaderTooltips.
    table_->horizontalHeaderItem(1)->setToolTip(
        tr("Where on the page: row×column, counting from the top-left. \"2×3\" is the "
           "second row, third pocket across."));
    table_->horizontalHeaderItem(2)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    auto* header = table_->horizontalHeader();
    // The name column (col 3) and the short metadata columns size to content; Set (col 4) is the
    // flexible slack absorber that grows when there's room and elides when space is tight —
    // mirroring OwnedCardsView. Prices (col 10) sizes to its "$… · €…" content.
    for (const int col : kAutoFitColumns) {  // all but the Set slack column
        header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    header->setSectionResizeMode(4, QHeaderView::Stretch);  // Set — flexible slack absorber
    // Cell padding so content clears the edges and the overlay scrollbar.
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");
    // EXPERIMENT: mark the 3x3-page boundaries (see PageBreakDelegate).
    table_->setItemDelegate(new PageBreakDelegate(table_));

    detail_ =
        new PokemonDetailPanel(media, wishlist, &cardImages_, &priceLookup_, &cardCopies_, this);

    connect(search_, &QLineEdit::textChanged, this, &BinderView::applyFilter);
    // Show the current row in the detail panel. currentCellChanged
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
        // The Page/Pocket columns aren't fields of the data — they ARE the filed position,
        // derived from the row order after sorting. So "sort by page" can only mean one
        // thing: drop back to the natural filed order (sortColumn_ < 0), which is also how
        // you undo a sort.
        sortColumn_ = column <= 1 ? -1 : column;
        sortOrder_ = order;
        repopulate();
    });
    // The detail panel's "Add copy" relays up to an in-place page push. Which of the two
    // it emits depends on the shown row: a species row keeps the species flow, a
    // species-free card's row switches the button to the "add a card" flow so it is never
    // a dead affordance (see showEntryInPanel).
    connect(detail_, &PokemonDetailPanel::addCopyRequested, this, &BinderView::openAddCopy);
    connect(detail_, &PokemonDetailPanel::addCardRequested, this, &BinderView::openAddCard);
    // In copy mode, "Edit card" relays up to an in-place edit-page push.
    connect(detail_, &PokemonDetailPanel::editCopyRequested, this, &BinderView::openEditCopy);
    // The "Wishlist (N)" button relays up to an in-place wishlist-page push.
    connect(detail_, &PokemonDetailPanel::editWishlistRequested, this, &BinderView::openWishlist);
    // The summary's "Manage prices" button relays up to an in-place prices-page push.
    connect(detail_, &PokemonDetailPanel::managePricesRequested, this, &BinderView::openPrices);
    // An inline fetch that auto-links a copy: write the id back into the cached copy store so a
    // re-selection shows it linked and the header value can count it once its prices land.
    connect(detail_, &PokemonDetailPanel::copyLinked, this,
            [this](const QString& copyId, const QString& externalCardId) {
                applyLinkedCardToVector(filedCopies_, copyId, externalCardId);
            });
    // A background auto-fetch (from adding a copy filed here) resolved that copy's link after
    // refresh() had already rebuilt the guide — write the id in so the auto-fetch's
    // pricesReady (below) isn't dropped by the anyCopyLinkedTo guard and its row/value fill in.
    connect(&priceLookup_, &CardPriceLookupService::copyAutoLinked, this,
            [this](const QString& copyId, const QString& externalCardId) {
                applyLinkedCardToVector(filedCopies_, copyId, externalCardId);
            });
    // A price fetch (from the detail panel, for a card filed here) can raise the binder's
    // market-value total AND fills in that card's row in the Prices column; both read the
    // local price cache, so re-read it and refresh the header + rows when such a card's
    // prices land rather than only on a full reopen. The lookup service is app-wide, so a
    // pricesReady for a card NOT filed here (most of them) is ignored instead of re-running
    // the batched cache read and rebuilding the table on every fetch anywhere.
    connect(&priceLookup_, &CardPriceLookupService::pricesReady, this,
            [this](const QString& externalCardId) {
                if (!anyCopyLinkedTo(filedCopies_, externalCardId)) {
                    return;
                }
                if (priceLookup_.bulkRunning()) {
                    // During a bulk, rewrite ONLY the affected Prices cells — a repopulate() per
                    // arriving price would re-sort and re-drive the detail panel (flicker). The
                    // sort and the value total are reconciled by the one full rebuild at
                    // bulkFinished.
                    updatePricesFor(externalCardId);
                } else {
                    // A single interactive event (Fetch/suppress/clear): a full rebuild keeps the
                    // sort correct and refreshes the value total. It's a one-off, so the single
                    // panel re-show is fine.
                    loadCachedPrices();
                    updateStats(filedCopies_);
                    repopulate();
                }
            });

    // The list (top bar + search + table) on the left, the detail panel on the
    // right, in a draggable horizontal split. The list takes the slack.
    // Row actions go UNDER the table, as on every other list screen (My Cards, Binders,
    // Wishlist). The split is by scope: an action that needs a selected row lives here;
    // the binder-wide ones (Add a card, Edit binder, Refresh prices) stay in the top bar.
    blankButton_ = new QPushButton(this);
    connect(blankButton_, &QPushButton::clicked, this, &BinderView::toggleBlankAtSelection);
    moveButton_ = new QPushButton(tr("Move…"), this);
    connect(moveButton_, &QPushButton::clicked, this, &BinderView::moveSelectedCard);
    auto* rowActions = new QHBoxLayout;
    rowActions->addWidget(blankButton_);
    rowActions->addWidget(moveButton_);
    rowActions->addStretch();

    auto* listPane = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    listLayout->addLayout(topBar);
    listLayout->addWidget(statsRow);
    listLayout->addWidget(search_);
    listLayout->addWidget(table_);
    listLayout->addLayout(rowActions);

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

    // No load here: showEvent runs refresh() when the page is first shown (and on every
    // tab return), so the guide always reflects current state without a redundant load at
    // construction. Mirrors OwnedCardsView.
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

    // The flat list of every copy filed here (species-free ones included) is this view's
    // backing store: each copy row reads its cells through it, and it drives the header
    // stats. A scoped read (listByBinder), not a whole-inventory scan — unlike the Pokémon
    // browser's listAll.
    std::vector<CardCopy> filed;
    try {
        filed = cardCopies_.listByBinder(binder_.id);
    } catch (const std::exception&) {
        // Now that a row IS a card, entries_ without the copies behind them is not a
        // degraded view but a lying one — rows would read "Completed" with every printed
        // column blank, and activating one would offer to add a card the binder already
        // holds. Drop the rows too, so a storage failure shows an empty guide.
        filed.clear();
        entries_.clear();
    }
    filedCopies_ = std::move(filed);

    // Index by id (for copyFor) and count the Owned copies per species (for the detail
    // panel's "N copies" line) in one pass over the fresh list. This map is a per-species
    // total over the whole binder and is deliberately NOT what "Captured" counts — that
    // reads the rows, so that a card moved out of its Pokédex slot leaves the slot reading
    // as uncollected instead of still counting toward it. The count stays bounded to the
    // catalog exactly as buildEntries bounds its species rows.
    copyIndexById_.clear();
    copyIndexById_.reserve(filedCopies_.size());
    ownedCountsByDex_.clear();
    for (std::size_t i = 0; i < filedCopies_.size(); ++i) {
        const CardCopy& copy = filedCopies_[i];
        copyIndexById_[copy.id] = i;
        if (copy.pokemonDexNum && copy.ownership == CardOwnership::Owned &&
            catalogEntry(*copy.pokemonDexNum) != nullptr) {
            ++ownedCountsByDex_[*copy.pokemonDexNum];
        }
    }

    loadCachedPrices();  // one batched cache read feeding both the header total and the rows
    updateStats(filedCopies_);
    updatePocketHeaderTooltips();  // the grid can have changed under Edit binder
    repopulate();
}

const CardCopy* BinderView::copyFor(const CardBinderEntry& entry) const {
    if (!entry.cardCopyId) {
        return nullptr;  // a placeholder row stands for no card
    }
    const auto it = copyIndexById_.find(*entry.cardCopyId);
    if (it == copyIndexById_.end() || it->second >= filedCopies_.size()) {
        return nullptr;
    }
    return &filedCopies_[it->second];
}

void BinderView::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // Reflect any copy or wishlist change made in another section (the Pokémon browser or
    // My Cards) since this was last shown — e.g. a Gastly copy moved between binders, which
    // otherwise left the guide stale on tab return. repopulate() restores the selection by
    // identity, so refreshing unconditionally still keeps the user's place. Mirrors
    // OwnedCardsView.
    refresh();
}

void BinderView::updatePricesFor(const QString& externalCardId) {
    // The in-place update used DURING a bulk only (single events take the full-rebuild path):
    // refresh this card's cache entry and rewrite the Prices cell of any row whose copy
    // carries it. No repopulate → no re-sort, no panel re-show; and NOT updateStats — the
    // value total (an O(copies) pass) is recomputed once by the full rebuild at bulkFinished
    // rather than on every arriving price. The sort is reconciled there too.
    const std::string id = externalCardId.toStdString();
    // Read both into locals first, then assign together — so a storage error on the second read
    // never leaves the price map refreshed against a stale suppression map.
    std::vector<CardPrice> prices;
    std::vector<std::string> suppressed;
    try {
        prices = priceLookup_.cachedPrices(externalCardId).prices;
        suppressed = priceLookup_.suppressedVendors(externalCardId);
    } catch (const std::exception&) {
        return;  // leave the current cells rather than crash on a storage error
    }
    pricesByExternalId_[id] = std::move(prices);
    suppressedByExternalId_[id] = std::move(suppressed);
    // Prices (col 10) is a ResizeToContents column, so every setItem re-measures it across
    // ALL rows — the O(rows^2) footgun table_bulk_update.h documents. This is the bulk
    // refresh's hot path (one call per arriving price) over a row set no longer bounded by
    // the catalog, and a duplicate-heavy binder writes one cell per copy rather than per
    // species, so the guard matters here even though only a few cells change: it drops the
    // column to Interactive for the writes and re-measures once at the end.
    BulkTablePopulate populateGuard(table_, kPriceColumnOnly);
    std::vector<CardPrice> scratch;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CardCopy* copy = copyFor(entries_[i]);
        // A Removed copy's Prices cell stays blank (frozen history), so skip it rather
        // than writing a figure the full rebuild would then wipe.
        if (copy == nullptr || isRemoved(*copy) || copy->externalCardId != id) {
            continue;
        }
        table_->setItem(
            i, 10,
            cell(priceAmountsInline(
                visiblePricesForCopy(pricesByExternalId_, suppressedByExternalId_, *copy, scratch),
                finishForFoil(copy->foil))));
    }
}

void BinderView::loadCachedPrices() {
    // Every non-Removed copy filed here can show a price, so all of them are read in ONE
    // batched cache query (loadCachedPricesFor) — a Removed copy is frozen history and its
    // cell stays blank, matching the inspector and My Cards. The value stat narrows this to
    // Owned itself. This is the only price read — both updateStats and repopulate() consult
    // the resulting map, so a header-sort reorder never re-queries.
    const auto notRemoved = [](const CardCopy& c) { return !isRemoved(c); };
    pricesByExternalId_ = loadCachedPricesFor(priceLookup_, filedCopies_, notRemoved);
    suppressedByExternalId_ = loadSuppressedVendorsFor(priceLookup_, filedCopies_, notRemoved);
}

void BinderView::updateStats(const std::vector<CardCopy>& filedCopies) {
    // Listed = the binder's CHECKLIST: the species it holds a reserved slot for. Not the
    // distinct species among the rows — an extra filed here (a Johto card in a Kanto
    // album) carries a species and a row without the binder ever reserving a slot for it,
    // so counting rows would push a 343-species checklist to 344 the moment one was filed.
    const std::set<PokemonDexNum> checklist = pokedex::listedSpecies(binder_, entries_);
    // With no rows there is nothing to report on. refresh() empties entries_ when the guide
    // fails to load, and a region-derived checklist would otherwise happily announce
    // "Listed 151 · Captured 0 (0%)" over a table that shows nothing — reading as a binder
    // full of uncollected species rather than one that could not be opened.
    const int listed = entries_.empty() ? 0 : static_cast<int>(checklist.size());

    // Captured = checklist slots that are FILLED — a listed species with at least one
    // Owned copy sitting in its own slot. Deliberately not ownedCountsByDex_'s key set,
    // which counts a species owned anywhere in this binder: a card the user pulled out of
    // its Pokédex slot and filed among the extras leaves that slot reading as uncollected,
    // and the figure above it has to agree with what the row says. Counting
    // CollectionStatus::Completed rows instead would miscount too, now that a species can
    // carry Completed, Incoming and Removed rows at once.
    // The placedByHand exclusion applies only where slots are RESERVED. A region-less
    // binder emits no placeholder when a card is pinned elsewhere, so excluding it there
    // would drop the species from the count with nothing standing in for it — the binder
    // would read "Listed 1 · Captured 0" over a single Owned card.
    const bool reserved = !binder_.pokemonRegions.empty();
    std::unordered_set<int> filledSlots;
    for (const CardBinderEntry& entry : entries_) {
        if (!entry.pokemon || !entry.cardCopyId || (reserved && entry.placedByHand)) {
            continue;
        }
        const CardCopy* copy = copyFor(entry);
        if (copy != nullptr && copy->ownership == CardOwnership::Owned &&
            checklist.contains(entry.pokemon->dexNumber)) {
            filledSlots.insert(entry.pokemon->dexNumber);
        }
    }
    const int captured = static_cast<int>(filledSlots.size());

    // Cards = physical copies in the sleeves right now: every Owned copy filed here,
    // duplicates counted separately and species-free cards included. Deliberately NOT a
    // species figure — that is what "Captured" is for — so it can be eyeballed against a
    // binder's capacity. Incoming (not here yet) and Removed (gone) are excluded.
    //
    // Market value of those same cards, totalled per currency; no FX conversion, so USD
    // (TCGplayer) and EUR (Cardmarket) stay separate. Read from the pricesByExternalId_
    // snapshot loadCachedPrices() built (network-free, no re-query), so a copy contributes
    // only once its prices were fetched (and it is linked); the figure is a lower bound
    // over what has been priced. Every copy still counts (three of a card is worth 3×), by
    // looking its id up in the map. One pass does both, since they share a predicate.
    int cardsHere = 0;
    std::map<std::string, long long> totals;
    std::vector<CardPrice> scratch;  // reused across copies; filled only for suppressed cards
    for (const CardCopy& copy : filedCopies) {
        if (copy.ownership != CardOwnership::Owned) {
            continue;
        }
        ++cardsHere;
        if (copy.externalCardId.empty()) {
            continue;
        }
        accumulateBestPrices(
            totals,
            visiblePricesForCopy(pricesByExternalId_, suppressedByExternalId_, copy, scratch),
            finishForFoil(copy.foil));
    }

    listedStat_->setText(tr("Listed %1").arg(listed));
    if (listed > 0) {
        // percentLabel guards both rounding extremes so the figure never contradicts the
        // count beside it (see binder_layout_labels.h).
        capturedStat_->setText(
            tr(" · Captured %1 (%2)").arg(captured).arg(percentLabel(captured, listed)));
    }
    // A regionless binder holding only Trainer cards lists no species — hide the ratio
    // rather than divide by zero, and let "Cards" lead instead.
    capturedStat_->setVisible(listed > 0);
    // With a recorded capacity, say how full the album is. Deliberately unclamped: a
    // binder stuffed past its rated capacity is a real thing the app never blocks, so
    // "Cards 400 of 360 (111%)" is the honest reading and exactly what the figure is for.
    const QString cards =
        binder_.capacity ? tr("Cards %1 of %2 (%3)")
                               .arg(cardsHere)
                               .arg(*binder_.capacity)
                               .arg(percentLabel(cardsHere, *binder_.capacity))
                         : tr("Cards %1").arg(cardsHere);
    cardsStat_->setText(listed > 0 ? QStringLiteral(" · ") + cards : cards);
    // Cards is the first visible stat when nothing is listed, so it drops its separator
    // there; Listed hides with it for the same reason.
    listedStat_->setVisible(listed > 0);
    const QString value = formatMoneyTotals(totals);
    valueStat_->setText(QStringLiteral(" · ") + value);
    valueStat_->setVisible(!value.isEmpty());
}

void BinderView::repopulate() {
    // Remember which row the detail panel is showing before the rebuild. A header-sort
    // reorders the rows, so the selection must be restored by IDENTITY — the shown COPY's
    // id first (a species can hold several rows now, and restoring to merely "the species"
    // would silently move the highlight, and every row action, to a different physical
    // card), falling back to the dex number for a placeholder row. Restoring by row index
    // would be wrong outright. Empty / -1 when nothing is shown.
    const QString shownCopyBefore = detail_->shownCopyId();
    const bool hadSelection = !table_->selectedItems().isEmpty();
    const int selectedDex = hadSelection ? shownDex_ : -1;

    sortEntries();

    // Rebuild every row from the freshly computed entries; entries_ and table rows
    // stay 1:1 and aligned. Statuses are fixed until the next refresh, so filtering
    // then just toggles row visibility — no per-keystroke allocation.
    //
    // Wrap the fill in BulkTablePopulate: the content-sized columns re-scan every row on
    // each setItem, so replacing ~9000 cells in an already-populated, visible ~1000-row
    // guide (the tab-return path) would otherwise freeze the UI for many seconds — the
    // guard turns that O(rows^2) refill into a single trailing re-measure. The columns
    // here are kAutoFitColumns — the same ResizeToContents set configured in the ctor (all
    // but the Stretch Set column, col 2). Scoped to the fill loop only; the later
    // applyFilter/panel work wants the normal resize modes back.
    //
    // The Page/Pocket columns are filled only when this binder records a pocket grid AND
    // the guide is in its natural filed order. Under any header sort the rows no longer
    // follow the sleeves, so a position would be a lie; the columns stay present but empty
    // (clicking either header is also the only way back to filed order, so hiding them
    // would remove the escape hatch).
    const int perPage = binder_.pocketGrid ? pocketsPerPage(*binder_.pocketGrid) : 0;
    const int columns = binder_.pocketGrid ? binder_.pocketGrid->columns : 0;
    const bool showPockets = perPage > 0 && sortColumn_ < 0;
    {
        BulkTablePopulate populateGuard(table_, kAutoFitColumns);
        table_->setRowCount(static_cast<int>(entries_.size()));
        std::vector<CardPrice> priceScratch;  // reused across rows (visiblePricesForCopy's
                                              // contract): fills only for a suppressed-vendor card,
                                              // so a whole rebuild allocates for those rows, not
                                              // every priced row
        // Removed rows render grayed out (the palette's disabled text color), as in My
        // Cards, so frozen history reads as inactive rather than as a card still in the
        // sleeves.
        const QBrush removedForeground =
            table_->palette().brush(QPalette::Disabled, QPalette::Text);
        int pocket = 0;  // 0-based sleeve position; advances only for rows that hold one
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            const CardBinderEntry& entry = entries_[i];
            const CardCopy* copy = copyFor(entry);
            // The page this slot falls on, so a searched-for row still says where the card
            // physically lives once the filter has hidden its neighbours.
            //
            // The predicate (everything holds a pocket EXCEPT a Removed copy, blanks and
            // placeholders included) is the shared domain one, NOT a local re-derivation:
            // the move planner counts pockets with the same function, and a disagreement
            // on any row would land a moved card in the wrong sleeve.
            const bool takesPocket = holdsPocket(entry);
            const bool numbered = showPockets && takesPocket;
            const int indexInPage = perPage > 0 ? pocket % perPage : 0;
            // Columns 0/1 are ALWAYS given an item, even when empty: the Removed-graying
            // pass below walks every column and would dereference a null cell otherwise.
            // A plain empty item rather than cell(), whose em-dash means "this record has
            // no data" — wrong when the whole column simply doesn't apply.
            auto* page =
                numbered ? cell(QString::number(pocket / perPage + 1)) : emptyCell();
            page->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (numbered) {
                page->setData(kPageEndRole, indexInPage == perPage - 1);
            }
            if (takesPocket) {
                ++pocket;
            }
            auto* pocketCell =
                numbered ? cell(pocketLabel(indexInPage, columns)) : emptyCell();
            // A card sitting somewhere its Pokédex number didn't put it says so on hover —
            // otherwise its row is indistinguishable from one that fell there naturally,
            // and the user has no way to tell what they arranged from what the dex did.
            //
            // Read from the row rather than re-derived from binder_.cardPlacements: only
            // the guide knows which placements it actually honoured, so a local rebuild
            // would badge a card whose orphaned placement left it in natural order anyway.
            if (entry.placedByHand) {
                const QString moved = tr("Moved to this pocket by hand. Use “Move…” to "
                                         "change it or return it to Pokédex order.");
                page->setToolTip(moved);
                pocketCell->setToolTip(moved);
            }
            table_->setItem(i, 0, page);
            table_->setItem(i, 1, pocketCell);
            // "#" is blank for a species-free card — it has no Pokédex number.
            auto* number =
                cell(entry.pokemon ? QString::number(entry.pokemon->dexNumber) : QString());
            number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(i, 2, number);
            table_->setItem(i, 3, cell(rowLabel(entry, copy)));
            // The row's OWN filed copy fills the printed-identity columns; a placeholder
            // row leaves them blank (rendered as an em-dash by cell()).
            // Set is the eliding Stretch column ("Base Set (BS)"); carry the full value as a
            // tooltip so a long name stays readable when the column truncates it ("…").
            const QString setText = copy ? setLabel(copy->cardRef) : QString();
            auto* setCell = cell(setText);
            setCell->setToolTip(setText);
            table_->setItem(i, 4, setCell);
            table_->setItem(i, 5, cell(copy ? QString::fromStdString(copy->cardRef.collectorNumber)
                                            : QString()));
            table_->setItem(i, 6, cell(copy && copy->condition ? conditionAbbrev(*copy->condition)
                                                               : QString()));
            table_->setItem(i, 7,
                            cell(copy && copy->rarity ? rarityLabel(*copy->rarity) : QString()));
            table_->setItem(i, 8, cell(copy && copy->foil ? foilLabel(*copy->foil) : QString()));
            // A blank pocket stands for no species and no card, so it reports no status.
            table_->setItem(i, 9,
                            cell(entry.status ? statusLabel(*entry.status) : QString()));
            // The copy's cached market prices, inline ("$… · €…"); blank when the copy is
            // unlinked, its prices were never fetched, or it is Removed (frozen history —
            // matches the inspector). Cache-only (pricesByExternalId_), so this stays a pure
            // in-memory rebuild — no network, no re-query.
            table_->setItem(
                i, 10,
                cell(copy && !isRemoved(*copy)
                         ? priceAmountsInline(
                               visiblePricesForCopy(pricesByExternalId_, suppressedByExternalId_,
                                                    *copy, priceScratch),
                               finishForFoil(copy->foil))
                         : QString()));
            if (copy && isRemoved(*copy)) {
                for (int col = 0; col < table_->columnCount(); ++col) {
                    table_->item(i, col)->setForeground(removedForeground);
                }
            }
        }
    }  // BulkTablePopulate re-measures the content columns once here

    // Move the highlight to the row the shown record landed on after the sort, so the
    // selection follows the record rather than the row index. Block signals so
    // setCurrentCell doesn't re-fire showRow; the panel is re-driven explicitly below.
    if (hadSelection) {
        const int restored = rowOf(shownCopyBefore, selectedDex);
        table_->blockSignals(true);
        if (restored >= 0) {
            table_->setCurrentCell(restored, 3);  // the name column
        } else {
            // The record left the guide entirely (its copy was deleted or moved to another
            // binder, and its species isn't in one of this binder's regions). DROP the
            // highlight — leaving it put would silently move the selection, the inspector
            // and every row action onto whatever unrelated record now occupies that row
            // index. Mirrors reselectRow()'s not-found path.
            table_->clearSelection();
            table_->setCurrentCell(-1, -1);
        }
        table_->blockSignals(false);
        if (restored < 0) {
            clearPanel();
        }
    }

    applyFilter(search_->text());  // preserve the current filter across a refresh

    // Re-drive the panel from the (identity-restored) current row so the highlight and
    // panel stay in agreement, mirroring OwnedCardsView. Every row names exactly one
    // record, so this is a plain re-show — there is no longer a random copy to avoid
    // re-rolling. openEditCopy re-selects the just-edited copy after its own refresh(),
    // which overrides this.
    const int current = table_->currentRow();
    if (current >= 0 && !table_->isRowHidden(current) && !table_->selectedItems().isEmpty()) {
        showEntryInPanel(current);
    }
}

void BinderView::sortEntries() {
    // Precompute each row's key once (via sortByKeys) rather than rebuilding it for both
    // operands on every comparison — the name and the copy-derived text columns allocate
    // QStrings, and the copy columns do a copyFor() lookup. A sortColumn_ < 0 keeps the
    // guide's NATURAL order, and only there does the "a species' copies are adjacent,
    // species-free cards last" guarantee hold — any header sort is free to interleave
    // them, which is the point of sorting. The dex number and the copy columns are keyed
    // as std::optional so a species-free row (no dex) and a placeholder row (no copy) sink
    // to the bottom in either direction (see compareOptional), not just ascending.
    // Condition/rarity/foil rank by enum value (best-to-worst condition; declaration order
    // for rarity/foil) so the sort matches My Cards' semantics rather than the labels'
    // alphabetical order.
    struct Key {
        std::optional<int> dexNumber;
        QString name;
        std::optional<QString> setText;
        std::optional<QString> collector;
        std::optional<int> conditionRank;
        std::optional<int> rarityRank;
        std::optional<int> foilRank;
        std::optional<int> statusRank;
        std::optional<long long> priceCents;
    };
    const bool ascending = sortOrder_ == Qt::AscendingOrder;
    const int column = sortColumn_;
    sortByKeys(
        entries_, sortColumn_, sortOrder_,
        [this, column](const CardBinderEntry& e) {
            // Build ONLY the clicked column's key (as OwnedCardsView::repopulate does):
            // the comparator reads a single field, so materializing every column on each
            // header click — five QString allocations plus a copyFor() lookup per row — is
            // pure waste that grows with the guide. Only the copy-derived columns (4–8, 10)
            // need the row's copy, so the dex/status columns skip that lookup entirely.
            // Unset fields keep their default (empty QString / 0 / nullopt), which the
            // comparator never consults for other columns. There are no cases 0/1: the
            // Page/Pocket columns are the filed position itself, and clicking one resets
            // sortColumn_ to -1 (natural order), which sortByKeys returns on before ever
            // calling this.
            Key key;
            switch (column) {
                case 2:
                    if (e.pokemon) {
                        key.dexNumber = e.pokemon->dexNumber;
                    }
                    break;
                case 3:
                    key.name = rowLabel(e, copyFor(e));
                    break;
                case 4:
                    if (const CardCopy* copy = copyFor(e)) {
                        key.setText = setLabel(copy->cardRef);
                    }
                    break;
                case 5:
                    if (const CardCopy* copy = copyFor(e)) {
                        key.collector = QString::fromStdString(copy->cardRef.collectorNumber);
                    }
                    break;
                case 6:
                    if (const CardCopy* copy = copyFor(e); copy && copy->condition) {
                        key.conditionRank = static_cast<int>(*copy->condition);
                    }
                    break;
                case 7:
                    if (const CardCopy* copy = copyFor(e); copy && copy->rarity) {
                        key.rarityRank = static_cast<int>(*copy->rarity);
                    }
                    break;
                case 8:
                    if (const CardCopy* copy = copyFor(e); copy && copy->foil) {
                        key.foilRank = static_cast<int>(*copy->foil);
                    }
                    break;
                case 9:
                    // A blank pocket has no status, so it stays nullopt and sinks to the
                    // bottom in either direction (compareOptional) rather than sorting as
                    // though it were some particular verdict.
                    if (e.status) {
                        key.statusRank = static_cast<int>(*e.status);
                    }
                    break;
                case 10:
                    // A Removed copy shows no price, so it must not sort as though it had
                    // one — it stays nullopt and sinks with the unpriced rows.
                    if (const CardCopy* copy = copyFor(e); copy && !isRemoved(*copy)) {
                        // Sort by the copy's representative value: the sum of its per-vendor
                        // figures in raw cents, USD and EUR added WITHOUT an FX rate (the same
                        // intentional tradeoff as the price table's amount sort — a rough
                        // magnitude ordering, not an exact worth). A copy with no cached price
                        // stays nullopt so it sinks to the bottom in either direction.
                        // visiblePricesForCopy returns a reference (into the price map, or into
                        // `scratch` for a suppressed card); vendorBest returns pointers into it,
                        // so both must outlive `best`'s reads below.
                        std::vector<CardPrice> scratch;
                        const std::vector<CardPrice>& visible = visiblePricesForCopy(
                            pricesByExternalId_, suppressedByExternalId_, *copy, scratch);
                        const VendorBest best = vendorBest(visible, finishForFoil(copy->foil));
                        if (best.tcg || best.cm) {
                            key.priceCents = (best.tcg ? best.tcg->amountCents : 0) +
                                             (best.cm ? best.cm->amountCents : 0);
                        }
                    }
                    break;
            }
            return key;
        },
        [ascending](const Key& a, const Key& b, int column) -> int {
            const auto text = [](const QString& x, const QString& y) {
                return x.localeAwareCompare(y);
            };
            const auto rank = [](int x, int y) { return compareValues(x, y); };
            switch (column) {
                case 2:
                    return compareOptional(a.dexNumber, b.dexNumber, ascending, rank);
                case 3:
                    return a.name.localeAwareCompare(b.name);
                case 4:
                    return compareOptional(a.setText, b.setText, ascending, text);
                case 5:
                    return compareOptional(a.collector, b.collector, ascending, text);
                case 6:
                    return compareOptional(a.conditionRank, b.conditionRank, ascending, rank);
                case 7:
                    return compareOptional(a.rarityRank, b.rarityRank, ascending, rank);
                case 8:
                    return compareOptional(a.foilRank, b.foilRank, ascending, rank);
                case 9:
                    // CollectionStatus enum values are the documented precedence order —
                    // a more meaningful grouping than the status labels' alphabetical order.
                    // A blank pocket carries none and sinks either way.
                    return compareOptional(a.statusRank, b.statusRank, ascending, rank);
                case 10:
                    return compareOptional(a.priceCents, b.priceCents, ascending,
                                           [](long long x, long long y) { return compareValues(x, y); });
            }
            return 0;
        });
}

int BinderView::rowOf(const QString& copyId, int dex) const {
    if (!copyId.isEmpty()) {
        const std::string id = copyId.toStdString();
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (entries_[i].cardCopyId && *entries_[i].cardCopyId == id) {
                return i;
            }
        }
    }
    // The copy is gone (moved, deleted) or was never named — fall back to the species,
    // whose placeholder or first remaining copy row is the nearest thing to where the
    // user was.
    if (dex >= 0) {
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            if (entries_[i].pokemon && entries_[i].pokemon->dexNumber == dex) {
                return i;
            }
        }
    }
    return -1;
}

void BinderView::applyFilter(const QString& filter) {
    // Identity, not species: a filter that hides the shown ROW must clear the panel even
    // when another copy of the same species is still visible.
    const int shownRow = rowOf(detail_->shownCopyId(), shownDex_);
    bool shownStillVisible = false;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CardBinderEntry& entry = entries_[i];
        const bool visible = rowMatchesFilter(entry, copyFor(entry), filter);
        table_->setRowHidden(i, !visible);
        if (visible && i == shownRow) {
            shownStillVisible = true;
        }
    }
    updateBlankButtonState();  // the selected row may have just been hidden
    updateMoveButtonState();
    if (shownStillVisible) {
        return;
    }
    // The row shown in the detail panel is now hidden (an empty result, or a filter that
    // excludes it), so clear the panel — it must never show a record with no visible row.
    clearPanel();
    // But a filter never moves the HIGHLIGHT, so widening one back out (or clearing the
    // search) leaves a row still selected with an empty panel — and, because the panel is
    // where the shown-copy id lives, that row would then be inert: activateRow's
    // confirm-then-edit needs a copy on screen. Re-drive the panel from the still-selected
    // row so selection and inspector never diverge.
    const int current = table_->currentRow();
    if (current >= 0 && current < static_cast<int>(entries_.size()) &&
        !table_->isRowHidden(current) && !table_->selectedItems().isEmpty()) {
        showEntryInPanel(current);
    }
}

void BinderView::showRow(int row) { showEntryInPanel(row); }

void BinderView::showEntryInPanel(int row) {
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    const CardBinderEntry& entry = entries_[row];
    const CardCopy* copy = copyFor(entry);
    if (copy) {
        // A copy row IS one exact card. Set the panel's sticky per-row state BEFORE
        // showing, so its button update sees the final mode: a species-free card has no
        // species to add a copy of and no wishlist, so Add becomes the "add a card" flow
        // rather than a dead disabled button.
        detail_->setAddMode(entry.pokemon ? PokemonDetailPanel::AddMode::SpeciesCopy
                                          : PokemonDetailPanel::AddMode::FreeCard);
        detail_->setWishlistVisible(entry.pokemon.has_value());
        shownDex_ = entry.pokemon ? entry.pokemon->dexNumber : -1;
        // "N copies" counts the species' owned copies filed here; a species-free card has
        // no such total, so 0 hides the line.
        int sameSpeciesTotal = 0;
        if (entry.pokemon) {
            const auto it = ownedCountsByDex_.find(entry.pokemon->dexNumber);
            sameSpeciesTotal = it == ownedCountsByDex_.end() ? 0 : it->second;
        }
        detail_->showSingleCopy(*copy, sameSpeciesTotal);
        updateBlankButtonState();
        updateMoveButtonState();
        return;
    }
    // A blank pocket: it stands for no species and no card, so there is nothing to
    // inspect. (This branch used to be unreachable defensive code; the blank row is what
    // now makes an entry naming neither a real, expected shape.)
    if (!entry.pokemon) {
        clearPanel();  // which also re-labels the blank button for this row
        return;
    }
    // A placeholder row: the species is listed but holds nothing here, so there is only
    // its artwork to show.
    detail_->setAddMode(PokemonDetailPanel::AddMode::SpeciesCopy);
    detail_->setWishlistVisible(true);
    shownDex_ = entry.pokemon->dexNumber;
    detail_->showPokemon(shownDex_, QString::fromStdString(entry.pokemon->name));
    updateBlankButtonState();
    updateMoveButtonState();
}

void BinderView::clearPanel() {
    // Reset the sticky per-row state too: leaving it in FreeCard mode would keep Add
    // enabled (that mode is deliberately selection-independent) with nothing shown.
    detail_->setAddMode(PokemonDetailPanel::AddMode::SpeciesCopy);
    detail_->setWishlistVisible(true);
    detail_->clear();
    shownDex_ = -1;
    updateBlankButtonState();
    updateMoveButtonState();
}

void BinderView::activateRow(int row) {
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    const CardBinderEntry& entry = entries_[row];
    const CardCopy* copy = copyFor(entry);
    // A Removed copy is frozen history: it cannot be edited (CardCopyService rejects it)
    // and it depicts a card that is no longer here, so its row is inert.
    if (copy && isRemoved(*copy)) {
        return;
    }
    // A species-free row whose copy couldn't be resolved (the copy read failed while the
    // guide's own read succeeded) names neither a card to edit nor a species to add — the
    // add branch would otherwise open the add page for dex -1.
    if (copy == nullptr && !entry.pokemon) {
        return;
    }
    const QString label = rowLabel(entry, copy);
    // The ROW's own copy, not the detail panel's: a row now IS one exact card, so its own
    // id is the authoritative target. Reading it back out of the panel (which is what a
    // species row used to have to do, since it held a random copy of the species) would
    // make the row inert whenever the two are out of step — e.g. right after a filter
    // cleared the panel while leaving the row highlighted.
    const QString copyId =
        entry.cardCopyId ? QString::fromStdString(*entry.cardCopyId) : QString();
    const int dex = entry.pokemon ? entry.pokemon->dexNumber : -1;
    activateCopyRow(
        {this, copy != nullptr,
         tr("%1 has no cards filed in this binder yet.\nAdd one now?").arg(label),
         tr("Edit card"), tr("Edit this card of %1?").arg(label),
         /*ownedNeedsShownCopy=*/true, copyId,
         [this, dex, label]() { openAddCopy(dex, label); },
         [this, copyId]() { openEditCopy(copyId); }});
}

void BinderView::pushAddPage(std::optional<PokemonDexNum> dexNumber, const QString& speciesName) {
    // Scoped to this binder: the copy is filed here and the picker is locked to it. A dex
    // number scopes the finder to that species' printings; nullopt puts it in by-name mode
    // for a card that depicts none.
    auto* page =
        new AddCardCopyPage(cardSearch_, cardCopies_, priceLookup_, binders_, cardImages_,
                            dexNumber, speciesName, binder_.id);
    // Adding a copy recomputes the guide: the new copy is filed in this binder, so it
    // gains a row of its own, and submit auto-returns — refresh so the guide isn't stale
    // on the way back.
    connect(page, &AddCardCopyPage::copyAdded, this, &BinderView::refresh);
    connect(page, &AddCardCopyPage::backRequested, this, [this, page]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

void BinderView::openAddCopy(int dexNumber, const QString& name) {
    pushAddPage(dexNumber, name);
}

void BinderView::openAddCard() { pushAddPage(std::nullopt, QString()); }

void BinderView::openEditCopy(const QString& copyId) {
    const CardCopy* copy = nullptr;
    const auto it = copyIndexById_.find(copyId.toStdString());
    if (it != copyIndexById_.end() && it->second < filedCopies_.size()) {
        copy = &filedCopies_[it->second];
    }
    // Gone from this binder, or frozen history — nothing to edit either way (the panel
    // hides Edit for a Removed copy and the service rejects editDetails on one).
    if (copy == nullptr || isRemoved(*copy)) {
        return;
    }
    // Capture the shown species by VALUE before pushing: refresh() may clear shownDex_,
    // and `copy` points into filedCopies_, which refresh() reassigns wholesale.
    const int dex = shownDex_;
    pushEditCopyPage(stack_, cardSearch_, priceLookup_, cardImages_, cardCopies_, *copy,
                     binders_.list(), [this, copyId, dex]() {
                         // An edit can change the guide (a comment, a binder move that
                         // removes the copy from here, a new image), so recompute, then
                         // re-show the SAME copy.
                         refresh();
                         reselectRow(copyId, dex);
                     });
}

void BinderView::openPrices(const QString& copyId) {
    const CardCopy* copy = nullptr;
    const auto it = copyIndexById_.find(copyId.toStdString());
    if (it != copyIndexById_.end() && it->second < filedCopies_.size()) {
        copy = &filedCopies_[it->second];
    }
    if (copy == nullptr) {
        return;
    }
    // Push the shown copy onto the prices page. On a Fetch there, write the resolved link back
    // into the cached copy store so a re-selection shows it linked and the value can count it;
    // on Back, refresh (a Clear/hide changes the Prices column + value total) and re-show the
    // same row. A price fetch on the page also emits pricesReady, which the handler
    // above already folds into the header + rows live.
    const int dex = shownDex_;
    pushPricesPage(
        stack_, priceLookup_, cardCopies_, *copy,
        [this](const QString& id, const QString& externalCardId) {
            applyLinkedCardToVector(filedCopies_, id, externalCardId);
        },
        [this, dex, copyId]() {
            refresh();
            reselectRow(copyId, dex);
        });
}

void BinderView::openWishlist(int dexNumber, const QString& name) {
    auto* page = new WishlistEditPage(wishlist_, dexNumber, name);
    // Capture the shown copy so Back re-shows the same species/copy. A wishlist change
    // can flip the species' CollectionStatus (Missing↔Wished), so recompute the guide
    // before re-selecting — mirroring the edit-copy return.
    const QString copyId = detail_->shownCopyId();
    connect(page, &WishlistEditPage::backRequested, this, [this, page, dexNumber, copyId]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
        refresh();
        reselectRow(copyId, dexNumber);
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

std::optional<CardBinderBlank> BinderView::blankAnchorForRow(int row) const {
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return std::nullopt;
    }
    const CardBinderEntry& entry = entries_[row];
    CardBinderBlank anchor;
    anchor.blanks = 1;
    // A card the user MOVED here anchors to itself even when it has a species: its row no
    // longer sits with its species' other rows, so a species anchor would open the gap in
    // front of those instead — somewhere else in the binder entirely.
    if (entry.cardCopyId &&
        std::any_of(binder_.cardPlacements.begin(), binder_.cardPlacements.end(),
                    [&entry](const CardBinderPlacement& p) {
                        return p.cardCopyId == *entry.cardCopyId;
                    })) {
        anchor.beforeCopyId = *entry.cardCopyId;
        return anchor;
    }
    if (entry.pokemon) {
        // Anchor to the SPECIES, not the copy: it survives that card being deleted and
        // re-added, and it is the unit the user is thinking in ("Kalos starts here").
        // Consequence, spelled out in the button's tooltip: on a species holding several
        // copies the blank lands before the first of them, not before the clicked row.
        anchor.beforeDexNum = entry.pokemon->dexNumber;
        return anchor;
    }
    if (entry.cardCopyId) {
        // A species-free card has no dex number to name it, so it anchors to itself.
        anchor.beforeCopyId = *entry.cardCopyId;
        return anchor;
    }
    return std::nullopt;  // a blank row anchors nothing of its own
}

void BinderView::updateBlankButtonState() {
    const int row = table_->currentRow();
    const bool haveRow = row >= 0 && row < static_cast<int>(entries_.size()) &&
                         !table_->isRowHidden(row) && !table_->selectedItems().isEmpty();
    const bool onBlank = haveRow && isBlankSlot(entries_[row]);

    blankButton_->setText(onBlank ? tr("Remove blank") : tr("Insert blank"));
    if (!binder_.pocketGrid) {
        blankButton_->setEnabled(false);
        blankButton_->setToolTip(
            tr("Set this binder's pocket grid in “Edit binder” first — without it there are "
               "no pages to break."));
        return;
    }
    if (sortColumn_ >= 0) {
        // The anchor would still be well defined under a sort, but "push what follows onto
        // the next page" is only legible in filed order. The tooltip doubles as the hint
        // for how to get back there.
        blankButton_->setEnabled(false);
        blankButton_->setToolTip(
            tr("Blank pockets can only be arranged in page order — click the Page column "
               "heading to return to it."));
        return;
    }
    if (onBlank) {
        blankButton_->setEnabled(true);
        blankButton_->setToolTip(tr("Remove this deliberately empty pocket."));
        return;
    }
    blankButton_->setEnabled(haveRow && blankAnchorForRow(row).has_value());
    blankButton_->setToolTip(
        haveRow ? tr("Leave a pocket empty before this row, pushing everything after it "
                     "further along the page. On a Pokémon with several cards filed here, "
                     "the gap goes before the first of them.")
                : tr("Select a row to leave a pocket empty before it."));
}

void BinderView::toggleBlankAtSelection() {
    const int row = table_->currentRow();
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return;
    }
    const bool removing = isBlankSlot(entries_[row]);
    // A blank row carries no identity of its own, so removing works off the row it sits
    // before: scan forward to the first row that does anchor one. Sound because the action
    // is gated to natural (filed) order, where "the next real row" is exactly the anchor
    // this blank was recorded against.
    std::optional<CardBinderBlank> anchor;
    if (removing) {
        for (int i = row + 1; i < static_cast<int>(entries_.size()); ++i) {
            anchor = blankAnchorForRow(i);
            if (anchor) {
                break;
            }
        }
    } else {
        anchor = blankAnchorForRow(row);
    }
    if (!anchor) {
        return;
    }

    try {
        binder_ = removing ? binders_.removeBlanks(binder_.id, *anchor)
                           : binders_.insertBlanks(binder_.id, *anchor);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not change this binder's blank pockets:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
        return;
    }
    showToast(this, removing ? tr("Blank pocket removed.") : tr("Blank pocket inserted."));

    // Rebuild, then put the highlight back on the ANCHOR row rather than the blank — so
    // pressing Insert again widens the same gap, which is how a page gets padded out.
    const QString anchorCopyId =
        anchor->beforeCopyId ? QString::fromStdString(*anchor->beforeCopyId) : QString();
    const int anchorDex = anchor->beforeDexNum.value_or(-1);
    refresh();
    reselectRow(anchorCopyId, anchorDex);
}

bool BinderView::rowIsMovable(int row) const {
    if (row < 0 || row >= static_cast<int>(entries_.size())) {
        return false;
    }
    const CardCopy* copy = copyFor(entries_[row]);
    // A placeholder and a blank hold a pocket but no card, and a Removed copy is frozen
    // history that isn't in a sleeve at all — none of the three is a card to re-file.
    return copy != nullptr && !isRemoved(*copy);
}

void BinderView::updateMoveButtonState() {
    const int row = table_->currentRow();
    const bool haveRow = row >= 0 && row < static_cast<int>(entries_.size()) &&
                         !table_->isRowHidden(row) && !table_->selectedItems().isEmpty();

    if (!binder_.pocketGrid) {
        // Without a grid there are no page/pocket coordinates to name, so the whole
        // feature has nothing to work in terms of — the same gate the blank button uses.
        moveButton_->setEnabled(false);
        moveButton_->setToolTip(
            tr("Record this binder's pocket grid in “Edit binder” first — without it there "
               "are no pockets to move a card to."));
        return;
    }
    if (sortColumn_ >= 0) {
        moveButton_->setEnabled(false);
        moveButton_->setToolTip(
            tr("Cards can only be re-filed in page order — click the Page column heading "
               "to return to it."));
        return;
    }
    moveButton_->setEnabled(haveRow && rowIsMovable(row));
    moveButton_->setToolTip(
        haveRow && rowIsMovable(row)
            ? tr("Put this card in a pocket you name. If that pocket is empty the card "
                 "just fills it; otherwise the cards in between shift along, and you'll be "
                 "told how many before anything is saved.")
            : tr("Select a card to move it to another pocket."));
}

void BinderView::moveSelectedCard() {
    const int row = table_->currentRow();
    if (!rowIsMovable(row) || !binder_.pocketGrid || sortColumn_ >= 0) {
        return;
    }
    const CardCopyId copyId = *entries_[row].cardCopyId;

    // Label every row the way the table's own Name column does, so the dialog's "that
    // pocket holds…" line names a card exactly as the row the user is looking at.
    std::vector<QString> rowLabels;
    rowLabels.reserve(entries_.size());
    for (const CardBinderEntry& entry : entries_) {
        rowLabels.push_back(rowLabel(entry, copyFor(entry)));
    }

    // Read the guide's verdict, never the raw placement records: a placement whose anchor
    // no longer emits is rejected, so the row sits in natural order with no "moved by hand"
    // badge — and offering "Return to natural order" for it would have the dialog
    // contradict the table about the same row.
    const bool placed = entries_[row].placedByHand;

    MoveCardDialog dialog(binder_, entries_, rowLabels, copyId,
                          rowLabel(entries_[row], copyFor(entries_[row])), placed, this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    try {
        if (dialog.resetRequested()) {
            binder_ = binders_.applyMove(binder_.id, planCardReset(binder_, entries_, copyId));
            showToast(this, tr("Card returned to Pokédex order."));
        } else {
            // Aiming at the pocket the card already occupies is a no-op, and must stay one.
            // The dialog pre-fills the card's CURRENT position, so pressing Move without
            // touching the spinboxes is an easy accident — and it is not harmless: taking
            // the card off a reserved slot opens a placeholder there, so "move it where it
            // already is" would silently grow the binder by a pocket and shift every card
            // after it along.
            int pocket = 0;
            for (const CardBinderEntry& entry : entries_) {
                if (!holdsPocket(entry)) {
                    continue;
                }
                if (entry.cardCopyId == copyId) {
                    break;
                }
                ++pocket;
            }
            if (pocket == dialog.targetPocket()) {
                showToast(this, tr("That card is already in that pocket."));
                return;
            }
            // The lookup lets the planner project the placeholder a vacated Pokédex slot
            // leaves behind with the verdict the guide will actually give it — it reads
            // the wishlist and the other binders, which a pure planner can't.
            const BinderMovePlan plan =
                planCardMove(binder_, entries_, copyId, dialog.targetPocket(),
                             [this](PokemonDexNum dex) {
                                 return guide_.placeholderStatusFor(binder_.id, dex);
                             });
            // Only ask when something actually has to be re-sleeved. A card dropped into
            // an empty pocket displaces nothing, and prompting there would train the user
            // to click through the warning that matters.
            if (plan.shiftedCards > 0) {
                const QMessageBox::StandardButton answer = QMessageBox::question(
                    this, tr("Move card"),
                    tr("This will shift %n other card(s) along by one pocket. Move it "
                       "anyway?",
                       nullptr, plan.shiftedCards),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (answer != QMessageBox::Yes) {
                    return;
                }
            }
            binder_ = binders_.applyMove(binder_.id, plan);
            showToast(this, tr("Card moved."));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Pokedex TCG"),
            tr("Could not move this card:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }

    refresh();
    reselectRow(QString::fromStdString(copyId), -1);  // follow the card to its new row
}

void BinderView::updatePocketHeaderTooltips() {
    table_->horizontalHeaderItem(0)->setToolTip(
        binder_.pocketGrid
            ? tr("Which page of the binder this slot falls on, counting %1 pockets per page.")
                  .arg(pocketsPerPage(*binder_.pocketGrid))
            : tr("Record this binder's pocket grid in “Edit binder” to see page numbers."));
}

void BinderView::openEditBinder() {
    auto* page = new BinderEditPage(binders_, binder_);
    // The page hands the whole persisted binder back on save, so binder_ is replaced in
    // place — no storage re-read. On Back, refresh() rebuilds the guide from the updated
    // binder_ (a grid or capacity change repages it; the regions can't change);
    // on a plain cancel, binder_ is unchanged and refresh() is a harmless recompute.
    connect(page, &BinderEditPage::saved, this, [this](const CardBinder& updated) {
        // Replace the whole value rather than patching field by field: this view never
        // re-reads binder_ from storage, so a wholesale swap is what keeps every field —
        // including the blanks the edit form doesn't touch, and anything added later —
        // in step with what was just persisted.
        binder_ = updated;
        heading_->setText(binderComboLabel(binder_));
    });
    pushBackablePage(stack_, page, [this]() { refresh(); });
}

void BinderView::reselectRow(const QString& copyId, int dex) {
    const int row = rowOf(copyId, dex);
    if (row < 0) {
        // The row left the guide entirely (a species-free card moved away, or a species'
        // last copy did and it wasn't in one of the binder's regions).
        clearPanel();
        return;
    }
    table_->blockSignals(true);  // setCurrentCell would re-fire showRow redundantly
    table_->setCurrentCell(row, 3);  // the name column
    table_->blockSignals(false);
    // Drive the panel explicitly: if the copy is gone this lands on the species'
    // placeholder row and falls back to plain artwork.
    showEntryInPanel(row);
}

}  // namespace pokedex
