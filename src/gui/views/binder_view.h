#pragma once

#include <QWidget>

#include <unordered_map>
#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"
#include "core/domain/card_copy.h"
#include "core/domain/types.h"

class QLineEdit;
class QStackedWidget;
class QTableWidget;

namespace pokedex {

class BinderGuideService;
class WishlistService;
class MediaService;
class CardSearchService;
class CardCopyService;
class CardImageStore;
class BinderService;
class PokemonDetailPanel;

// GUI — the "open binder" screen: the binder's guide as a list of its Pokémon,
// each paired with its CollectionStatus, above a live partial-name search box.
// A thin shell over BinderGuideService (the Qt-free verb). It computes the entries
// on construction and re-filters the cached list as the user types; adding a copy
// from here recomputes the guide (a new copy can change an "owned elsewhere"
// status), so returning from Add-copy shows current state rather than stale.
//
// This is an embedded page, not a separate window: BindersWindow shows it inside
// a QStackedWidget and a "Back" button (which emits backRequested) returns to the
// binder list. Opening a binder is in-app navigation, not a modal detour.
class BinderView : public QWidget {
    Q_OBJECT

public:
    BinderView(BinderGuideService& guide, const CardBinder& binder,
               WishlistService& wishlist, MediaService& media, CardSearchService& cardSearch,
               CardCopyService& cardCopies, CardImageStore& cardImages, BinderService& binders,
               QWidget* parent = nullptr);

Q_SIGNALS:
    // Emitted when the user asks to leave this page (the Back button). The owner
    // navigates back to the binder list and disposes of this view.
    void backRequested();

private:
    // (Re)compute the guide's entries from the source of truth and rebuild the table
    // (via repopulate()). Run once on construction and again after a copy is added
    // from this page (statuses may have changed).
    void refresh();
    // Sort the already-computed entries_ and rebuild the table rows, re-applying the
    // filter and re-showing the current copy. This is the pure in-memory path a
    // header-sort click takes — it never recomputes the guide or re-reads the copies.
    void repopulate();
    // Sort entries_ in place by the active header column/order before repopulate()
    // populates the rows. A negative sortColumn_ keeps the guide's natural (dex)
    // order (the initial, unsorted state).
    void sortEntries();
    // Show only the rows whose Pokémon name contains `filter` (case-insensitive);
    // an empty filter shows all. Rows are toggled, not rebuilt.
    void applyFilter(const QString& filter);
    // Show the clicked/selected row's Pokémon in the detail panel. Reads the dex
    // number and name from the row's cells (columns 0 and 1).
    void showRow(int row);
    // Double-click / Enter on a row: if the species has a copy filed here, confirm and
    // open its edit page (the copy the panel is showing); otherwise offer to add one to
    // this binder. Mirrors the Pokémon browser's activate gesture and My Cards'
    // double-click-to-edit.
    void activateRow(int row);
    // Push an AddCardCopyPage for `dexNumber` onto the inner stack; its Back pops
    // and disposes it, returning to the binder guide.
    void openAddCopy(int dexNumber, const QString& name);
    // Push an EditCardCopyPage for the owned copy `copyId` (the one the detail panel
    // is showing) onto the inner stack; Back pops it, then refreshes the guide and
    // re-shows the current row so an edit (comment, binder move, image) is reflected.
    void openEditCopy(const QString& copyId);
    // Move the highlight to species `dex`'s row and re-show its copy `copyId` in the
    // panel — restoring the selection by IDENTITY after a refresh() rebuilt the rows.
    // Clears the panel if the species left the guide. Called by the edit-page return.
    void reselectSpecies(int dex, const QString& copyId);

    BinderGuideService& guide_;
    CardBinder binder_;
    CardSearchService& cardSearch_;
    CardCopyService& cardCopies_;
    CardImageStore& cardImages_;
    BinderService& binders_;
    QStackedWidget* stack_;
    QTableWidget* table_;
    QLineEdit* search_;
    PokemonDetailPanel* detail_;
    std::vector<CardBinderEntry> entries_;
    // Header-driven sort state, re-applied on every refresh so it survives a
    // recompute. -1 = unsorted (keep the guide's dex order); see sortEntries().
    int sortColumn_ = -1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    // Owned copies filed in this binder, bucketed by species dex, rebuilt on every
    // refresh(). Drives copy mode in the detail panel: a species present here has at
    // least one owned copy filed in this binder to show.
    std::unordered_map<PokemonDexNum, std::vector<CardCopy>> ownedHere_;
    // Dex number currently shown in the detail panel (-1 = none), so a filter that
    // hides its row can clear the panel rather than leave stale artwork on screen.
    int shownDex_ = -1;
};

}  // namespace pokedex
