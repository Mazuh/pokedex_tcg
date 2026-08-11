#pragma once

#include <QWidget>

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"
#include "core/domain/card_copy.h"
#include "core/domain/types.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace pokedex {

class BinderGuideService;
class WishlistService;
class MediaService;
class CardSearchService;
class CardPriceLookupService;
class CardCopyService;
class CardImageStore;
class BinderService;
class PokemonDetailPanel;
class BulkRefreshController;

// GUI — the "open binder" screen: the binder's guide above a live partial-name
// search box. A thin shell over BinderGuideService (the Qt-free verb), which
// decides the rows; this view renders, sorts and filters them.
//
// A ROW IS A SLOT, NOT A SPECIES (see CardBinderEntry). Every card filed in the
// binder gets its own row — several copies of one Pokémon appear side by side, and
// a Trainer/Energy card, which depicts no species at all, lands after every species
// row — while a listed species holding nothing keeps a single placeholder row, the
// Pokédex checklist this screen has always been. So the row set is
// `entries_`, and `entries_` stays strictly 1:1 with the table's rows: the filter
// HIDES rows, never removes them, and every index-keyed path (sort, selection
// restore, price cell rewrites) depends on that.
//
// Because a row names one exact copy, the detail panel is driven with
// showSingleCopy() rather than a random pick among a species' copies; row identity
// is therefore the copy id (with the dex number as a fallback for placeholder rows)
// — see rowOf().
//
// Adding or editing a copy from here recomputes the guide (it can change a species'
// status, or add/remove a row outright), so returning from a pushed page shows
// current state rather than stale.
//
// This is an embedded page, not a separate window: BindersPage shows it inside
// a QStackedWidget and a "Back" button (which emits backRequested) returns to the
// binder list. Opening a binder is in-app navigation, not a modal detour.
class BinderView : public QWidget {
    Q_OBJECT

public:
    BinderView(BinderGuideService& guide, const CardBinder& binder,
               WishlistService& wishlist, MediaService& media, CardSearchService& cardSearch,
               CardPriceLookupService& priceLookup, CardCopyService& cardCopies,
               CardImageStore& cardImages, BinderService& binders, QWidget* parent = nullptr);

Q_SIGNALS:
    // Emitted when the user asks to leave this page (the Back button). The owner
    // navigates back to the binder list and disposes of this view.
    void backRequested();

protected:
    // Re-read the guide whenever this page is (re-)shown so a copy OR wishlist source
    // edited/moved/removed in another section (the Pokémon browser or My Cards) isn't
    // left stale here — both feed a row's status and its printed columns. refresh() →
    // repopulate() restores the selection by identity, so the user's place survives the
    // rebuild. Mirrors OwnedCardsView::showEvent; the binder guide was the one section
    // that lacked this and so showed stale data on tab return. This is also the guide's
    // only load (there is no load in the ctor — the first show does it).
    void showEvent(QShowEvent* event) override;

private:
    // (Re)compute the guide's entries from the source of truth and rebuild the table
    // (via repopulate()). Run on every show and again after a copy is added or edited
    // from this page (rows and statuses may have changed).
    void refresh();
    // Sort the already-computed entries_ and rebuild the table rows, re-applying the
    // filter and re-showing the current row. This is the pure in-memory path a
    // header-sort click takes — it never recomputes the guide or re-reads the copies.
    void repopulate();
    // Sort entries_ in place by the active header column/order before repopulate()
    // populates the rows. A negative sortColumn_ keeps the guide's natural order
    // (species by dex with their copies adjacent, species-free cards last).
    void sortEntries();
    // Recompute the header stats (Listed / Captured / % / Cards / market $ value) from
    // the freshly computed entries_ and the binder's filed copies, and set the four stat
    // labels. Called from refresh() once both are loaded — never from repopulate(), since
    // a header-sort is a pure reorder that changes neither the counts nor the value.
    void updateStats(const std::vector<CardCopy>& filedCopies);
    // (Re)read the local price cache for every non-Removed copy filed here into
    // pricesByExternalId_, a single batched cache read (no network). Feeds both the
    // header value stat and the per-row Prices column, so both draw from one snapshot.
    // Non-Removed rather than Owned-only so an Incoming row can show a price too; the
    // value stat re-checks for Owned itself. Best-effort: a storage failure leaves the
    // map empty rather than crashing.
    void loadCachedPrices();
    // Rewrite just the Prices cell(s) for `externalCardId` and the value total from the refreshed
    // cache — a price-only update that avoids a full repopulate() (which would re-sort and
    // re-drive the detail panel). Used for every pricesReady, so a bulk refresh's stream of
    // events and an interactive suppress/clear both reflect live without flicker.
    void updatePricesFor(const QString& externalCardId);
    // Show only the rows whose Pokémon/card name contains `filter` (case-insensitive);
    // an empty filter shows all. Rows are toggled, not rebuilt.
    void applyFilter(const QString& filter);
    // Show the clicked/selected row in the detail panel, reading entries_[row] — never
    // the cell text, which renders an empty dex number as an em-dash.
    void showRow(int row);
    // Double-click / Enter on a row: a copy row confirms and opens that copy's edit page;
    // a placeholder row offers to add a copy of the species to this binder. A Removed
    // copy's row is inert (there is nothing legal to do with frozen history). Mirrors
    // the Pokémon browser's activate gesture and My Cards' double-click-to-edit.
    void activateRow(int row);
    // Push an AddCardCopyPage onto the inner stack, locked to this binder; its Back pops
    // and disposes it, returning to the binder guide. A dex number makes it the species'
    // add-copy page (finder species-scoped); nullopt makes it the species-free "add a
    // card" page (finder in by-name mode). Shared by both entry points below.
    void pushAddPage(std::optional<PokemonDexNum> dexNumber, const QString& speciesName);
    // The species add-copy flow, from a placeholder row's activation or the panel's Add.
    void openAddCopy(int dexNumber, const QString& name);
    // The species-free add flow, from the top bar's "Add a card" (always available, no
    // row selection needed) or the panel's Add while a species-free row is shown.
    void openAddCard();
    // Push an EditCardCopyPage for the filed copy `copyId` (the one the detail panel is
    // showing) onto the inner stack; Back pops it, then refreshes the guide and re-shows
    // the same copy so an edit (comment, binder move, image) is reflected.
    void openEditCopy(const QString& copyId);
    // Push a WishlistEditPage for species `dexNumber` onto the inner stack; Back pops
    // it, then recomputes the guide (a wishlist change can flip a placeholder row's
    // CollectionStatus, e.g. Incomplete↔Wished) and re-shows the same row.
    void openWishlist(int dexNumber, const QString& name);
    void openPrices(const QString& copyId);
    // Push a BinderEditPage for this binder onto the inner stack; Back pops it, then
    // re-reads the binder (its name/region may have changed) — updating the heading
    // and recomputing the guide, since a region change alters the species list.
    void openEditBinder();
    // Move the highlight to the row for copy `copyId` (falling back to species `dex`)
    // and re-show it in the panel — restoring the selection by IDENTITY after a refresh()
    // rebuilt the rows. Clears the panel if the row left the guide. Called by the
    // edit/prices/wishlist page returns.
    void reselectRow(const QString& copyId, int dex);
    // The row showing copy `copyId`, or — when that copy is gone or `copyId` is empty —
    // the first row for species `dex`; -1 when neither is present. The single definition
    // of row identity: a sort or a rebuild moves records between row indices, so every
    // "find my row again" path must go through this rather than a remembered index.
    int rowOf(const QString& copyId, int dex) const;
    // Drive the detail panel from entries_[row]: a copy row shows that exact copy
    // (with the Add/wishlist affordances matching whether it depicts a species), a
    // placeholder row shows the species' plain artwork. The one place that dispatch lives.
    void showEntryInPanel(int row);
    // Empty the panel AND reset its sticky per-row state (Add mode, wishlist visibility)
    // back to the species defaults — otherwise a species-free row that gets filtered away
    // leaves Add stuck in the always-enabled FreeCard mode with nothing selected.
    void clearPanel();
    // The copy an entry stands for, or nullptr for a placeholder row. Looked up by id in
    // filedCopies_ via copyIndexById_. The returned pointer is valid only until
    // filedCopies_ is next rebuilt (refresh()), so never capture it in a page-return
    // lambda — capture the copy id and look it up again.
    const CardCopy* copyFor(const CardBinderEntry& entry) const;

    BinderGuideService& guide_;
    CardBinder binder_;
    WishlistService& wishlist_;
    CardSearchService& cardSearch_;
    CardPriceLookupService& priceLookup_;
    CardCopyService& cardCopies_;
    CardImageStore& cardImages_;
    BinderService& binders_;
    QStackedWidget* stack_;
    QLabel* heading_;  // the binder's name in the top bar; re-set after an edit
    QTableWidget* table_;
    // The header stats, one label per figure so each can carry its own explanatory
    // tooltip (a single rich-text label can't). Muted via one stylesheet on their
    // container; each carries its own leading " · " separator so hiding a stat takes
    // its separator with it.
    QLabel* listedStat_;
    QLabel* capturedStat_;
    QLabel* cardsStat_;
    QLabel* valueStat_;
    QLineEdit* search_;
    PokemonDetailPanel* detail_;
    QPushButton* refreshPricesButton_;  // "Refresh prices" — bulk re-fetch all filed cards
    QLabel* bulkStatus_;                 // "Refreshing… n/m" progress beside it (hidden when idle)
    // The guide's rows, strictly 1:1 with the table's rows (see the class docstring).
    std::vector<CardBinderEntry> entries_;
    // Header-driven sort state, re-applied on every refresh so it survives a
    // recompute. -1 = unsorted (keep the guide's natural order); see sortEntries().
    int sortColumn_ = -1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    // How many Owned copies of each species are filed here, rebuilt on every refresh().
    // Feeds the "Captured" stat (its size) and the panel's "N copies" line. A count, not
    // the copies themselves: every row already names the exact copy it stands for.
    std::unordered_map<PokemonDexNum, int> ownedCountsByDex_;
    // The full flat list of copies filed here (incl. species-free ones), cached from the
    // last refresh() — the backing store every copy row's cells read through, and what the
    // header value stat is recomputed from in-session (when a copy is auto-linked or its
    // prices are fetched) without a re-query.
    std::vector<CardCopy> filedCopies_;
    // copy id → index into filedCopies_, rebuilt alongside it. An INDEX, not a pointer, so
    // it can't silently outlive the vector's wholesale reassignment in refresh().
    std::unordered_map<std::string, std::size_t> copyIndexById_;
    // The local price cache for the copies filed here, keyed by external card id, read
    // once per refresh (loadCachedPrices) so both the header value stat and the per-row
    // Prices column read from one snapshot and a header-sort never re-queries.
    std::unordered_map<std::string, std::vector<CardPrice>> pricesByExternalId_;
    // The suppressed vendors for those same copies (keyed by external card id), read in the same
    // pass so a hidden vendor is left out of the Prices column, its sort, and the binder value
    // total, exactly as the inspector's headline hides it. Empty for the no-suppression case.
    std::unordered_map<std::string, std::vector<std::string>> suppressedByExternalId_;
    // Dex number of the species currently shown in the detail panel (-1 = none, including
    // when a species-free card is shown). The panel's artwork/wishlist context, and the
    // fallback half of row identity — NOT the row key; see rowOf().
    int shownDex_ = -1;
};

}  // namespace pokedex
