#pragma once

#include <QWidget>

#include <QString>
#include <unordered_map>
#include <vector>

#include "core/app/pokemon_browse_service.h"
#include "core/domain/card_copy.h"
#include "core/domain/types.h"

class QLabel;
class QLineEdit;
class QStackedWidget;
class QTableWidget;

namespace pokedex {

class MediaService;
class CardSearchService;
class CardPriceLookupService;
class CardCopyService;
class CardImageStore;
class BinderService;
class WishlistService;
class PokemonDetailPanel;

// GUI — the Pokémon section of the main window: an unscoped, read-only browse of
// the whole National Pokédex (# / name / region / owned-copy count) with a live
// partial-name search. A thin shell over PokemonBrowseService (the Qt-free
// verb): it computes the entries once on construction, then filters and paginates
// the cached list.
//
// The detail panel runs in copy mode here (unlike the read-only past): selecting a
// species that owns copies (in any binder) shows one of them. Double-clicking a row
// is a shortcut with a confirm prompt — on a species that owns nothing it offers to
// open the add-copy page, and on one that owns copies it offers to jump to "My Cards"
// pre-filtered to that species (emitting searchInMyCardsRequested for MainWindow to
// switch sections + set the filter). Editing a specific copy stays on the detail
// panel's explicit "Edit card…" button.
//
// The full catalog is ~1000 species, so the table loads incrementally
// (infinite scroll): it starts with one chunk of rows and appends the next
// chunk as the user scrolls near the bottom, keeping the initial render cheap
// and the view free of a 1025-row wall. It is an embedded section in
// MainWindow's stack, not a separate window.
class PokemonListView : public QWidget {
    Q_OBJECT

public:
    PokemonListView(PokemonBrowseService& service, WishlistService& wishlist,
                    MediaService& media, CardSearchService& cardSearch,
                    CardPriceLookupService& priceLookup, CardCopyService& cardCopies,
                    CardImageStore& cardImages, BinderService& binders,
                    QWidget* parent = nullptr);

    // Push this species' add-copy page onto the inner stack; its Back pops and disposes it,
    // returning to the browse splitter. `setQuery` (when non-empty) pre-seeds the finder's
    // "Find by set" search and nothing else — the scan flow passes it after detecting a
    // Pokémon so the user lands on that species' creation with the set already searched
    // (the host must make this section current first, so the pushed page shows). Public so
    // MainWindow can drive it; the detail panel's "Add copy" relays here too.
    void openAddCopy(int dexNumber, const QString& name, const QString& setQuery = QString());

signals:
    // Emitted when the user double-clicks a species that already owns copies: the host
    // (MainWindow) switches to the "My Cards" section and pre-filters its search to
    // `species` so the user can browse/act on those copies in the flat inventory.
    void searchInMyCardsRequested(const QString& species);

protected:
    // Watches the table viewport's resize so the list keeps filling a viewport
    // that grew taller than the loaded rows (where no scrollbar exists yet, so
    // scrolling can't drive the next load).
    bool eventFilter(QObject* watched, QEvent* event) override;
    // Re-read the catalog + owned copies each time this section becomes visible, so a
    // copy added or edited in another section (e.g. My Cards) is reflected — and, so
    // the cached owned_ that a browser-initiated edit saves back is never stale
    // (mirrors OwnedCardsView / WishlistView). Inner add/edit page pops don't re-show
    // this widget, so they don't trigger it (they refresh explicitly).
    void showEvent(QShowEvent* event) override;

private:
    // Rebuild the filtered index set from the search text, reset to the top, and
    // load enough to fill the viewport. Matches on name (case-insensitive
    // substring) or on the dex-number text, so "25" finds Pikachu and "char"
    // finds Charmander.
    void applyFilter();
    // Append the next chunk of filtered rows to the table (never rebuilds the
    // rows already shown). A no-op once every filtered row is loaded.
    void loadMore();
    // Append chunks until the loaded rows overflow the viewport (so a scrollbar
    // appears and scrolling can take over) or every filtered row is loaded.
    void fillViewport();
    // Refresh the "Showing N of M" status label from loadedCount_ / filtered_.
    void updateCountLabel();
    // Show the clicked/selected row's Pokémon in the detail panel. Reads the dex
    // number and name from the row's cells (columns 0 and 1), so it is immune to
    // the filtered_-index indirection. Hands the panel this species' owned copies
    // (copy mode) when it owns any, so one is shown.
    void showRow(int row);
    // Double-click / Enter on a row: a confirm-then-act shortcut. On a species that
    // owns nothing, prompt to open the add-copy page; on one that owns copies, prompt
    // to jump to "My Cards" filtered to that species (via searchInMyCardsRequested).
    void activateRow(int row);
    // Push an EditCardCopyPage for the owned copy `copyId` (the one the detail panel
    // is showing) onto the inner stack; Back pops it, then refreshes and re-shows the
    // just-edited copy so a change (comment, image, binder move) is reflected.
    void openEditCopy(const QString& copyId);
    // Push a WishlistEditPage for species `dexNumber` onto the inner stack; Back pops
    // it and re-shows the species so the detail panel's "Wishlist (N)" button reflects
    // any change. No table refresh — the browse columns don't depend on the wishlist.
    void openWishlist(int dexNumber, const QString& name);
    void openPrices(const QString& copyId);
    // Move the highlight to the row for species `dex` (loading rows until it exists,
    // since it can sit past the first chunk) and re-show its copy `copyId` in the
    // panel — restoring the selection by IDENTITY after a refresh() re-rendered the
    // list from the top. Clears the panel if the species is filtered out. Shared by
    // the edit-page return and showEvent's revisit restore.
    void reselectSpecies(int dex, const QString& copyId);
    // Drive the detail panel for species `dex`: copy mode when it owns copies
    // (preferring `preferCopyId` if set, else a random one), plain artwork otherwise.
    // The one place the owned_ lookup → showPokemon dispatch lives.
    void showSpeciesInPanel(int dex, const QString& name, const QString& preferCopyId = {});
    // Re-query the catalog + owned counts and re-render, so the Owned column
    // reflects a copy just added.
    void refresh();
    // (Re)bucket every owned, species-tied copy by dex number into owned_ — the
    // whole-inventory read (unscoped by binder) behind copy mode. Rebuilt on
    // construction and on every refresh(). Returns false if the inventory read
    // failed (owned_ left empty, the revision sentinel held) so refresh() can
    // preserve the rendered table rather than rebuild it to all-zeros.
    bool loadOwnedCopies();

    PokemonBrowseService& service_;
    WishlistService& wishlist_;
    CardSearchService& cardSearch_;
    CardPriceLookupService& priceLookup_;
    CardCopyService& cardCopies_;
    CardImageStore& cardImages_;
    BinderService& binders_;
    QStackedWidget* stack_;
    QLineEdit* search_;
    QTableWidget* table_;
    QLabel* countLabel_;
    PokemonDetailPanel* detail_;

    // The whole catalog, computed once; never re-queried.
    std::vector<PokemonBrowseEntry> entries_;
    // Indices into entries_ that pass the current filter, in dex order.
    std::vector<int> filtered_;
    // How many of filtered_ are currently rendered as table rows.
    int loadedCount_ = 0;
    // Dex number currently shown in the detail panel (-1 = none), so a filter that
    // hides it can clear the panel rather than leave stale artwork on screen.
    int shownDex_ = -1;
    // Owned copies bucketed by species dex, across all binders, rebuilt on every
    // refresh(). Drives copy mode in the detail panel and the double-click branch:
    // a species present here owns at least one copy to show / jump to in My Cards.
    std::unordered_map<PokemonDexNum, std::vector<CardCopy>> owned_;
    // Guards fillViewport() against re-entry: appending rows can make a
    // scrollbar appear, which resizes the viewport and re-fires the resize
    // event mid-fill — without this it would overshoot the rows actually needed.
    bool filling_ = false;
    // CardCopyService::revision() at the last SUCCESSFUL loadOwnedCopies(). showEvent
    // skips its full-inventory refresh (a whole card_copy read + ~1000-species catalog
    // rebuild) when this still matches — i.e. no copy was added/edited/removed in any
    // section since — keeping the section-switch hot path cheap for an unchanged
    // collection while preserving the user's scroll position and selection untouched.
    // The constructor's load stamps it, so the first visit refreshes only if a copy was
    // added/removed in another eagerly-built section between construction and that visit.
    // Held at the -1 sentinel after a failed read (never a real revision), so the gate
    // can't latch an empty fallback and suppress the retry on the next visit.
    long ownedRevision_ = -1;
};

}  // namespace pokedex
