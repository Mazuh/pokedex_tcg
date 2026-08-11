#include "gui/views/binder_view.h"

#include <QBrush>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPalette>
#include <QPushButton>
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
#include "gui/views/bulk_refresh_controller.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/condition_labels.h"
#include "gui/views/copy_row_activation.h"
#include "gui/views/edit_copy_page_host.h"
#include "gui/views/prices_page_host.h"
#include "gui/views/foil_labels.h"
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
#include "gui/views/wishlist_edit_page.h"

namespace pokedex {

namespace {
// The guide's auto-fit columns: every column that sizes to its content
// (ResizeToContents) — all but the Stretch Set column (2). The single source of truth
// for both the ctor's initial resize-mode setup and repopulate()'s BulkTablePopulate
// guard, so the two can't silently drift when a column is added, removed, or reordered
// (a mismatch would either reintroduce the O(rows^2) reopen freeze or convert the Set
// slack column). Note column 1 carries a Pokémon name OR a card name (a species-free
// row has no species to name it). Mirrors OwnedCardsView's kAutoFitColumns.
constexpr int kAutoFitColumns[] = {0, 1, 3, 4, 5, 6, 7, 8};

// The Prices column alone — the subset updatePricesFor() rewrites in place. It needs the
// same BulkTablePopulate treatment as a full rebuild (col 8 is content-sized), but must
// not disturb the other columns' modes, since nothing else on that path changes.
constexpr int kPriceColumnOnly[] = {8};

bool isRemoved(const CardCopy& copy) { return copy.ownership == CardOwnership::Removed; }

// What column 1 says for a row, and the string both the sort key and the search
// filter use — one helper so the three can't drift. A species row is named by its
// species (so a Pokémon's several copies read as one clean block, disambiguated by
// the Set/Collector columns); a species-free row falls back to the printed card
// name, exactly as My Cards' first column does.
QString rowLabel(const CardBinderEntry& entry, const CardCopy* copy) {
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
    editBinderButton->setToolTip(tr("Change this binder's name or region."));
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
    listedStat_->setToolTip(tr("Pokémon species this binder lists: every species in its "
                               "regions, plus any other species with a card filed here."));
    capturedStat_ = new QLabel(statsRow);
    capturedStat_->setToolTip(tr("How many of those species have at least one owned card "
                                 "filed in this binder, and what share of the listed "
                                 "species that is."));
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

    // A read-only table: dex number, name, then the printed-identity columns mirroring
    // My Cards (set, collector, condition, rarity, foil) for the row's own filed copy,
    // its Status, and finally that copy's cached market Prices ("$… · €…", cache-only —
    // never a network read). Whole-row selection, no editing. The Set column takes the
    // slack (as in My Cards); the name column sizes to content so it is never truncated.
    // A placeholder row (a listed species with nothing filed here — most rows in a fresh
    // binder) leaves the copy columns blank; a species-free card's row leaves "#" blank,
    // since it has no Pokédex number.
    table_ = new QTableWidget(this);
    table_->setColumnCount(9);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon / Card"),
                                       tr("Set name / expansion code"),
                                       tr("Collector"), tr("Cond."), tr("Rarity"), tr("Foil"),
                                       tr("Status"), tr("Prices")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // The "#" header sits over right-aligned dex numbers, so right-align it to match.
    table_->horizontalHeaderItem(0)->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    auto* header = table_->horizontalHeader();
    // The name column (col 1) and the short metadata columns size to content; Set (col 2) is the
    // flexible slack absorber that grows when there's room and elides when space is tight —
    // mirroring OwnedCardsView. Prices (col 8) sizes to its "$… · €…" content.
    for (const int col : kAutoFitColumns) {  // all but the Set slack column
        header->setSectionResizeMode(col, QHeaderView::ResizeToContents);
    }
    header->setSectionResizeMode(2, QHeaderView::Stretch);  // Set — flexible slack absorber
    // Cell padding so content clears the edges and the overlay scrollbar.
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

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
        sortColumn_ = column;
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
    auto* listPane = new QWidget(this);
    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(16, 12, 16, 12);  // match the other sections' padding
    listLayout->addLayout(topBar);
    listLayout->addWidget(statsRow);
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

    // Index by id (for copyFor) and count the Owned copies per species (for the Captured
    // stat and the panel's "N copies" line) in one pass over the fresh list. The count is
    // bounded to the catalog exactly as buildEntries bounds its species rows — otherwise a
    // copy carrying a dex number with no species (which buildEntries routes to the
    // species-free tail) would raise "Captured" above "Listed" and print >100%.
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
    // Prices (col 8) is a ResizeToContents column, so every setItem re-measures it across
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
            i, 8,
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
    // Listed = every SPECIES the guide lists — no longer entries_.size(), since a species
    // with several copies filed here now contributes several rows. Captured = species with
    // at least one Owned copy filed here, which is exactly ownedCountsByDex_'s key set.
    // Counting CollectionStatus::Completed rows would miscount now that a species can carry
    // Completed, Incoming and Removed rows at once.
    std::unordered_set<int> listedSpecies;
    listedSpecies.reserve(entries_.size());
    for (const CardBinderEntry& entry : entries_) {
        if (entry.pokemon) {
            listedSpecies.insert(entry.pokemon->dexNumber);
        }
    }
    const int listed = static_cast<int>(listedSpecies.size());
    const int captured = static_cast<int>(ownedCountsByDex_.size());

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
        const int percent = qRound(100.0 * captured / listed);
        // Guard both rounding extremes so the figure never contradicts the count: a tiny
        // nonzero ratio shows "<1%" rather than "0%", and a nearly-complete-but-not one
        // (e.g. 997/1000 → qRound(99.7) == 100) shows ">99%" rather than a false "100%".
        QString pct;
        if (percent == 0 && captured > 0) {
            pct = tr("<1%");
        } else if (percent == 100 && captured < listed) {
            pct = tr(">99%");
        } else {
            pct = tr("%1%").arg(percent);
        }
        capturedStat_->setText(tr(" · Captured %1 (%2)").arg(captured).arg(pct));
    }
    // A regionless binder holding only Trainer cards lists no species — hide the ratio
    // rather than divide by zero, and let "Cards" lead instead.
    capturedStat_->setVisible(listed > 0);
    cardsStat_->setText(listed > 0 ? tr(" · Cards %1").arg(cardsHere)
                                   : tr("Cards %1").arg(cardsHere));
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
        for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
            const CardBinderEntry& entry = entries_[i];
            // "#" is blank for a species-free card — it has no Pokédex number.
            auto* number =
                cell(entry.pokemon ? QString::number(entry.pokemon->dexNumber) : QString());
            number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(i, 0, number);
            const CardCopy* copy = copyFor(entry);
            table_->setItem(i, 1, cell(rowLabel(entry, copy)));
            // The row's OWN filed copy fills the printed-identity columns; a placeholder
            // row leaves them blank (rendered as an em-dash by cell()).
            // Set is the eliding Stretch column ("Base Set (BS)"); carry the full value as a
            // tooltip so a long name stays readable when the column truncates it ("…").
            const QString setText = copy ? setLabel(copy->cardRef) : QString();
            auto* setCell = cell(setText);
            setCell->setToolTip(setText);
            table_->setItem(i, 2, setCell);
            table_->setItem(i, 3, cell(copy ? QString::fromStdString(copy->cardRef.collectorNumber)
                                            : QString()));
            table_->setItem(i, 4, cell(copy && copy->condition ? conditionAbbrev(*copy->condition)
                                                               : QString()));
            table_->setItem(i, 5,
                            cell(copy && copy->rarity ? rarityLabel(*copy->rarity) : QString()));
            table_->setItem(i, 6, cell(copy && copy->foil ? foilLabel(*copy->foil) : QString()));
            table_->setItem(i, 7, cell(statusLabel(entry.status)));
            // The copy's cached market prices, inline ("$… · €…"); blank when the copy is
            // unlinked, its prices were never fetched, or it is Removed (frozen history —
            // matches the inspector). Cache-only (pricesByExternalId_), so this stays a pure
            // in-memory rebuild — no network, no re-query.
            table_->setItem(
                i, 8,
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
            table_->setCurrentCell(restored, 1);
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
        int statusRank = 0;
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
            // pure waste that grows with the guide. Only the copy-derived columns (2–6, 8)
            // need the row's copy, so the dex/status columns skip that lookup entirely.
            // Unset fields keep their default (empty QString / 0 / nullopt), which the
            // comparator never consults for other columns.
            Key key;
            switch (column) {
                case 0:
                    if (e.pokemon) {
                        key.dexNumber = e.pokemon->dexNumber;
                    }
                    break;
                case 1:
                    key.name = rowLabel(e, copyFor(e));
                    break;
                case 2:
                    if (const CardCopy* copy = copyFor(e)) {
                        key.setText = setLabel(copy->cardRef);
                    }
                    break;
                case 3:
                    if (const CardCopy* copy = copyFor(e)) {
                        key.collector = QString::fromStdString(copy->cardRef.collectorNumber);
                    }
                    break;
                case 4:
                    if (const CardCopy* copy = copyFor(e); copy && copy->condition) {
                        key.conditionRank = static_cast<int>(*copy->condition);
                    }
                    break;
                case 5:
                    if (const CardCopy* copy = copyFor(e); copy && copy->rarity) {
                        key.rarityRank = static_cast<int>(*copy->rarity);
                    }
                    break;
                case 6:
                    if (const CardCopy* copy = copyFor(e); copy && copy->foil) {
                        key.foilRank = static_cast<int>(*copy->foil);
                    }
                    break;
                case 7:
                    key.statusRank = static_cast<int>(e.status);
                    break;
                case 8:
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
                case 0:
                    return compareOptional(a.dexNumber, b.dexNumber, ascending, rank);
                case 1:
                    return a.name.localeAwareCompare(b.name);
                case 2:
                    return compareOptional(a.setText, b.setText, ascending, text);
                case 3:
                    return compareOptional(a.collector, b.collector, ascending, text);
                case 4:
                    return compareOptional(a.conditionRank, b.conditionRank, ascending, rank);
                case 5:
                    return compareOptional(a.rarityRank, b.rarityRank, ascending, rank);
                case 6:
                    return compareOptional(a.foilRank, b.foilRank, ascending, rank);
                case 7:
                    // CollectionStatus enum values are the documented precedence order —
                    // a more meaningful grouping than the status labels' alphabetical order.
                    return compareValues(a.statusRank, b.statusRank);
                case 8:
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
        return;
    }
    // A placeholder row: the species is listed but holds nothing here, so there is only
    // its artwork to show.
    if (!entry.pokemon) {
        clearPanel();  // defensive: an entry naming neither is never produced
        return;
    }
    detail_->setAddMode(PokemonDetailPanel::AddMode::SpeciesCopy);
    detail_->setWishlistVisible(true);
    shownDex_ = entry.pokemon->dexNumber;
    detail_->showPokemon(shownDex_, QString::fromStdString(entry.pokemon->name));
}

void BinderView::clearPanel() {
    // Reset the sticky per-row state too: leaving it in FreeCard mode would keep Add
    // enabled (that mode is deliberately selection-independent) with nothing shown.
    detail_->setAddMode(PokemonDetailPanel::AddMode::SpeciesCopy);
    detail_->setWishlistVisible(true);
    detail_->clear();
    shownDex_ = -1;
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

void BinderView::openEditBinder() {
    auto* page = new BinderEditPage(binders_, binder_);
    // The page hands the committed name/regions straight back on save, so update
    // binder_ (and the heading) in place — no storage re-read. On Back, refresh()
    // rebuilds the guide from the updated binder_ (a region change alters which
    // species it lists); on a plain cancel, binder_ is unchanged and refresh() is a
    // harmless recompute.
    connect(page, &BinderEditPage::saved, this,
            [this](const QString& name, const std::vector<Region>& regions) {
                binder_.name = name.toStdString();
                binder_.pokemonRegions = regions;
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
    table_->setCurrentCell(row, 1);
    table_->blockSignals(false);
    // Drive the panel explicitly: if the copy is gone this lands on the species'
    // placeholder row and falls back to plain artwork.
    showEntryInPanel(row);
}

}  // namespace pokedex
