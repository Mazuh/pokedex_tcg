#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

#include <vector>

#include "core/app/pokemon_external_api.h"
#include "core/domain/card_copy.h"

class QEvent;
class QLabel;
class QPushButton;

namespace pokedex {

class CardImageStore;
class CardPriceLookupService;
class CardCopyService;
class CardPricesSummary;
class MediaService;
class WishlistService;

// GUI — the single right-hand inspector shared by every card surface: the Pokémon
// browser, the binder guide, and the flat "My Cards" inventory. One widget class so
// the three read identically. It shows, top to bottom:
//   • the card's name (falling back to the Pokémon's name when a card records none),
//   • the printed collector identity (only in copy mode),
//   • the card image (the copy's own scan, falling back to the Pokémon's official
//     artwork when the copy has no saved scan and depicts a species),
//   • one line of condition + foil, one line of rarity + copy count ("N copies"),
//   • the copy's comments,
//   • the reusable market-prices block,
//   • an Add + Edit button row (side by side), and an optional "Wishlist (N)" button.
//
// It depends on MediaService for artwork and WishlistService only to read the sources
// count for the wishlist button's label, never on the external API's specifics.
//
// Copy mode is opt-in and needs a CardImageStore (for the copy's scan). The two
// species hosts enter it via showPokemon() with the species' owned copies (one is
// shown, chosen at random or by preferCopyId); My Cards enters it via showSingleCopy()
// with the exact selected copy. Passing a species with no copies (or constructing
// without an image store) shows the plain artwork-only state. The wishlist button and
// the species-scoped "Add copy" are for the species hosts; My Cards switches the Add
// button to the species-free "Add a card" flow (setAddMode) and hides the wishlist
// (setWishlistVisible), since it also holds Trainer/Energy cards that depict no species.
//
// showPokemon() displays the name immediately and asks MediaService for the artwork
// asynchronously, showing a loading placeholder meanwhile. Because results arrive out
// of order when the user clicks quickly, the panel records the dex it currently wants
// and ignores any ready()/failed() for a different one (the stale-guard). On failure
// it shows a "no image" placeholder — the name still stands and nothing crashes.
class PokemonDetailPanel : public QWidget {
    Q_OBJECT

public:
    // How the Add button behaves. SpeciesCopy (default) adds a copy for the shown
    // species (emits addCopyRequested with its dex); FreeCard adds a species-free card
    // and is always enabled, even with nothing selected (emits addCardRequested).
    enum class AddMode { SpeciesCopy, FreeCard };

    // `prices` and `copies`, when supplied together (alongside `images`), add the market-prices
    // block (CardPricesSummary) to copy mode: the per-vendor headline figures + marketplace links
    // + ⓘ, an inline Fetch/Refresh, and a "Manage prices" button that relays
    // managePricesRequested() so the host opens the dedicated PricesEditPage. The inline fetch can
    // resolve + link a copy, relayed up as copyLinked(). Passing either null (or no image store)
    // omits the block, leaving the artwork-only behavior.
    PokemonDetailPanel(MediaService& media, WishlistService& wishlist,
                       CardImageStore* images = nullptr, CardPriceLookupService* prices = nullptr,
                       CardCopyService* copies = nullptr, QWidget* parent = nullptr);

    // Show `name` now and request its artwork. Not named show() so it doesn't
    // hide QWidget::show().
    void showPokemon(int dexNumber, const QString& name);
    // As above, but with the species' owned copies. Non-empty enters copy mode (see the
    // class docstring); empty is identical to the 2-arg overload. `preferCopyId` names
    // the copy to show — used to keep the just-edited copy on screen when returning from
    // the edit page; when empty (a fresh row selection) one is picked at random.
    void showPokemon(int dexNumber, const QString& name,
                     const std::vector<CardCopy>& ownedCopiesHere,
                     const QString& preferCopyId = QString());
    // Show one exact copy (My Cards' selection model, where the user picks a specific
    // copy rather than a species). `sameSpeciesTotal` is how many copies of the same
    // species back the "N copies" line (0 hides it — e.g. a species-free card).
    void showSingleCopy(const CardCopy& copy, int sameSpeciesTotal);
    // Empty state: no selection.
    void clear();

    // Choose the Add button's flow (see AddMode). Defaults to SpeciesCopy.
    void setAddMode(AddMode mode);
    // Show or hide the "Wishlist (N)" button. Defaults to visible (the species hosts);
    // My Cards hides it, since a species-free card has no wishlist.
    void setWishlistVisible(bool visible);

    // The id of the copy currently shown in copy mode, or "" when not in copy mode
    // (plain artwork / empty state). Lets an owning view re-show the SAME copy after
    // a rebuild (e.g. a header-sort refresh) instead of re-rolling a random one.
    QString shownCopyId() const { return shownCopyId_; }

Q_SIGNALS:
    // The user asked to add a card copy for the shown Pokémon (AddMode::SpeciesCopy).
    // The panel is embedded, so the owning view turns this into a stack push to the
    // AddCardCopyPage scoped to the species.
    void addCopyRequested(int dexNumber, const QString& name);
    // The user asked to add a species-free card (AddMode::FreeCard). The owning view
    // pushes the species-free AddCardCopyPage.
    void addCardRequested();
    // The user asked to edit the copy currently shown in copy mode. Carries the
    // copy's id; the owning view opens the EditCardCopyPage for it.
    void editCopyRequested(const QString& copyId);
    // The user clicked the "Wishlist (N)" button for the shown Pokémon. Carries the
    // species; the owning view pushes the WishlistEditPage for it.
    void editWishlistRequested(int dexNumber, const QString& name);
    // The user clicked the summary's "Manage prices" button. Carries the shown copy's id; the
    // owning view pushes the PricesEditPage for it.
    void managePricesRequested(const QString& copyId);
    // The summary's inline Fetch resolved and persisted this copy's tcgdex link. Forwarded up so
    // the host writes the new external id into its cached copy (as PricesEditPage's cardLinked
    // does) — otherwise a re-selection re-runs the resolve and value stats keyed on the id miss it.
    void copyLinked(const QString& copyId, const QString& externalCardId);

protected:
    // Rescale the image when the image label resizes (on a panel resize, but also
    // when showing/hiding the copy widgets changes the label's height on its own).
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void onReady(int dexNumber, MediaKind kind, const QPixmap& pixmap);
    void onFailed(int dexNumber, MediaKind kind);
    void onCardImageChanged(const QString& copyId);  // re-read the shown copy's image
    // Populate (and show) the copy-detail block for `copy`, where `copyTotal` copies of
    // the same species exist (drives the "N copies" line; 0 hides it), and load its card
    // image (fallback to artwork on a null pixmap). The shared renderer behind both
    // showPokemon(copies) and showSingleCopy().
    void renderCopy(const CardCopy& copy, int copyTotal);
    // Show the copy's card scan (by copy id) when it has one, else fall back to
    // the Pokémon artwork. Shared by the initial show and the image-changed re-read.
    void showCopyImage(const std::string& copyId);
    // Hide the copy block, prices and edit button (plain / empty-state).
    void hideCopy();
    // Enable/label the Add/Edit/Wishlist buttons for the current mode and state.
    void updateButtons();
    // Set the wishlist button's label to the species' source count and enable it.
    void updateWishlistButton(int dexNumber);
    // Reset the image to the loading placeholder and (re)request the current
    // species' official artwork — the shared fallback when no copy scan is shown.
    // Shows the "no image" placeholder instead when there is no species (dex < 1).
    void requestArtworkFallback();
    // Scale originalPixmap_ to fit the image label, or show placeholder_ text
    // when there is no pixmap (empty/loading/failed states).
    void renderImage();

    MediaService& media_;
    WishlistService& wishlist_;
    CardImageStore* images_;  // null in the Pokémon browser → copy mode disabled
    CardPricesSummary* priceSummary_ = nullptr;  // null when the price service wasn't supplied
    AddMode addMode_ = AddMode::SpeciesCopy;
    bool wishlistVisible_ = true;
    int currentDex_ = -1;  // the dex we currently want shown; guards stale results
    // The shown species' name, kept separate from the name_ label's text. The label
    // titles by the CARD in copy mode (a "Charizard ex" printing reads "Charizard ex"),
    // but the species name is what the Add/Wishlist emissions and the artwork media key
    // need — so those read this, never name_->text(), which would leak the card title.
    // Empty for a species-free card (no species to name).
    QString speciesName_;
    QString shownCopyId_;  // id of the copy shown in copy mode ("" = not in copy mode)
    bool shownCopyRemoved_ = false;  // the shown copy is soft-Removed → no Edit affordance
    bool showingCopyImage_ = false;  // the image is a copy scan, not artwork → keep
    QLabel* name_;
    QLabel* collector_;  // printed collector identity ("BS 44/102"); copy mode only
    QLabel* image_;
    QLabel* condFoilLine_;      // "NM · Reverse Holo" (present parts only; hidden if none)
    QLabel* rarityCountLine_;   // "Rare Holo · 3 copies" (present parts only; hidden if none)
    QLabel* copyComments_;
    QWidget* copyDetail_;  // container for the copy detail lines; hidden outside copy mode
    QPushButton* addButton_;
    QPushButton* editButton_;
    QPushButton* wishlistButton_;  // "Wishlist (N)" — opens the species' wishlist page
    QPixmap originalPixmap_;  // full-resolution artwork; rescaled on resize
    QString placeholder_;     // text shown when there is no pixmap
};

}  // namespace pokedex
