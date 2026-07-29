#pragma once

#include <QString>
#include <QWidget>

#include <string>
#include <unordered_map>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_binder.h"
#include "core/domain/card_copy.h"

class QLabel;
class QLineEdit;
class QPushButton;
class QShowEvent;
class QStackedWidget;
class QTableWidget;

namespace pokedex {

class CardCopyService;
class BinderService;
class CardImageStore;
class CardSearchService;
class CardPriceLookupService;
class MediaService;
class WishlistService;
class PokemonDetailPanel;
class EditCardCopyPage;
class BulkRefreshController;
class AddCardCopyPage;

// GUI — the "My Cards" section: a flat, read-only inventory of every card copy the
// user has recorded (Owned, Incoming, or soft-Removed), so they can keep track of
// their collection. A thin shell over CardCopyService::listAll(); species names
// come from the compile-time Pokédex catalog. A right-hand panel shows the selected
// copy's stored card image (loaded from the workspace via CardImageStore); copies
// added before that image was saved, or without a preview picked, show a placeholder.
//
// It reloads every time it becomes visible (showEvent), so a copy added elsewhere
// (from a Pokémon's "Add copy" page) appears the moment the user switches here,
// without any cross-section signalling. A live search box filters the rows on any
// visible field, and a selected card can be filed into / out of a binder via a
// picker, or opened in an in-window "Edit card" page to change its image. It is a
// section embedded in MainWindow's stack, not a separate window; the edit page is
// pushed onto its own inner QStackedWidget (the PokemonListView list⇄page idiom).
//
// The right-hand detail is the shared PokemonDetailPanel (the same inspector the
// Pokémon browser and binder guide use), in its species-free-friendly configuration:
// the Add button records a species-free card and the wishlist button is hidden. The
// panel also hosts the "Edit card" action, so the toolbar keeps only the inventory-wide
// operations (Assign / Remove / Delete permanently).
class OwnedCardsView : public QWidget {
    Q_OBJECT

public:
    OwnedCardsView(CardCopyService& copies, BinderService& binders, CardImageStore& images,
                   CardSearchService& cardSearch, CardPriceLookupService& priceLookup,
                   MediaService& media, WishlistService& wishlist, QWidget* parent = nullptr);

    // Pre-fill the live search box with `text` (e.g. a species name) and apply it, so a
    // caller can open this section already narrowed to a set of copies. The text persists
    // through the reload showEvent triggers when this section next becomes visible, so it
    // is safe to call before switching here. Used by MainWindow when the Pokémon browser
    // asks to "search in My Cards" for a species.
    void searchFor(const QString& text);

protected:
    void showEvent(QShowEvent* event) override;

private:
    // Re-query all copies (and the binders), then rebuild the table (via repopulate()).
    // Run when the underlying data may have changed (edit/assign/remove/add, first show).
    void reload();
    // Sort the already-loaded copies and rebuild the rows/haystacks from the cached
    // data, re-selecting `keepSelectedId` at its new row. This is the pure in-memory
    // path a header-sort click takes — it never re-hits storage just to reorder rows.
    void repopulate(const std::string& keepSelectedId);
    // The copy id of the current selection, or empty when nothing is selected.
    std::string selectedCopyId() const;
    // The copy under the current row IF it is in-bounds and not hidden by the search
    // filter, else nullptr — the bounds+visibility guard every row action opens with (a
    // filter-hidden row keeps its selection, so acting on it would mutate an off-screen
    // card). Each caller layers its own ownership policy on top (Assign/Edit reject a
    // Removed copy, Delete requires one, Remove allows either). The pointer is valid
    // until loaded_ is next rebuilt.
    const CardCopy* selectedVisibleCopy() const;
    // Hide rows that don't match the search text (case-insensitive substring over
    // every visible column), and refresh the "Showing N of M" count.
    void applyFilter();
    // Enable the row-action buttons only when a row is selected.
    void updateButtonState();
    // Show the selected copy in the right-hand inspector, or clear it when there is no
    // selection. (Named showSelectedImage for historical continuity across the section.)
    void showSelectedImage();
    // How many live (non-Removed) copies of the selected copy's species are in the
    // inventory (drives the inspector's "N copies" line); 0 for a species-free card.
    int sameSpeciesCount(const CardCopy& copy) const;
    // (Re)read the local price cache for every linked copy in the inventory into
    // pricesByExternalId_, a single batched cache read (no network) run from reload() so a
    // header-sort's repopulate() never re-queries. Best-effort: a storage failure leaves
    // the map empty (blank Prices cells) rather than crashing.
    void loadCachedPrices();
    // Open the binder picker for the selected copy and file it accordingly.
    void assignSelected();
    // Soft-remove the selected copy, prompting for an optional note to append.
    void removeSelected();
    // Permanently delete the selected copy's row (enabled only for an already
    // soft-Removed copy), after an always-shown confirmation.
    void deletePermanently();
    // Push the in-window "Edit card" page for the selected copy (to change its image).
    void editSelectedCard();
    void openPrices(const QString& copyId);
    // Throttle a price-driven rebuild (loadCachedPrices + repopulate): coalesce a burst of
    // pricesReady (a bulk refresh, or rapid single fetches) into at most one rebuild per window,
    // so the table doesn't thrash — while still reflecting every event within the window.
    void schedulePriceReload();
    // Push the in-window "Add a card" page for a species-free card (a Trainer/Energy
    // card that depicts no Pokémon) — the only place such a card can be recorded, since
    // the Pokémon browser's "Add copy" is always scoped to a species.
    void addNewCard();

    CardCopyService& copies_;
    BinderService& binders_;
    CardImageStore& images_;
    CardSearchService& cardSearch_;   // transport for the edit page's card finder
    CardPriceLookupService& priceLookup_;  // transport for the inspector's prices block
    MediaService& media_;          // artwork fallback for a copy with no saved scan
    WishlistService& wishlist_;    // required by the shared inspector (its wishlist is hidden here)
    QStackedWidget* stack_;   // page 0 = list ⇄ inspector; page 1 = the edit page
    PokemonDetailPanel* panel_;   // right-hand inspector (shared with the other sections)
    QLineEdit* search_;
    QTableWidget* table_;
    // Header-driven sort state, re-applied on every reload so it survives a refresh.
    // -1 = unsorted (group by dex then age); see reload().
    int sortColumn_ = -1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    QLabel* emptyLabel_;   // shown in place of the table when no cards are recorded yet
    QPushButton* assignButton_;
    QPushButton* removeButton_;
    QPushButton* deleteButton_;   // "Delete permanently…" — enabled only for Removed copies
    QPushButton* refreshPricesButton_;  // "Refresh prices" — bulk re-fetch all linked copies
    QLabel* bulkStatus_;                // "Refreshing… n/m" progress (hidden when idle)
    bool priceReloadQueued_ = false;    // a throttled price rebuild is already scheduled
    QLabel* countLabel_;
    // The copies backing the current rows, in display order (row i ⇄ loaded_[i]);
    // filtering only hides rows, so this stays aligned with the table.
    std::vector<CardCopy> loaded_;
    // The binders, cached from the last reload() so a header-sort repopulate() can
    // resolve/sort the Binder column (name + region) without a second query.
    std::vector<CardBinder> binderList_;
    // The local price cache for the inventory's linked copies, keyed by external card id,
    // read once per reload() so the Prices column (and its sort) draw from one snapshot and
    // a header-sort never re-queries.
    std::unordered_map<std::string, std::vector<CardPrice>> pricesByExternalId_;
    // The suppressed vendors for those same linked copies (keyed by external card id), read in
    // the same reload() pass so a hidden vendor is filtered out of the Prices column and its
    // sort exactly as the inspector's headline hides it. Empty for the common no-suppression case.
    std::unordered_map<std::string, std::vector<std::string>> suppressedByExternalId_;
    // Per-row lowercased search text, precomputed in reload() so filtering is a
    // plain substring compare with no per-keystroke allocation (row i ⇄ haystacks_[i]).
    std::vector<QString> haystacks_;
    // The copy id currently shown in the panel (empty when the panel is cleared), so
    // showSelectedImage() can skip the disk read when the selection hasn't changed —
    // it fires on every keystroke via applyFilter().
    std::string shownCopyId_;
};

}  // namespace pokedex
