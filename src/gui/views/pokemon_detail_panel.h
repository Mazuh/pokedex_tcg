#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

#include <vector>

#include "core/app/pokemon_external_api.h"
#include "core/domain/card_copy.h"

class QLabel;
class QPushButton;

namespace pokedex {

class CardImageStore;
class MediaService;
class WishlistService;
class WishlistSourcesEditor;

// GUI — the right-hand detail panel: shows the selected Pokémon's name, its
// official artwork, and (below the art) its wishlist sources editor. Embedded
// (via a splitter) in both browsers, so one widget class serves the Pokémon list
// and the binder guide. It depends on MediaService for artwork and WishlistService
// for the sources editor, never on the external API's specifics.
//
// showPokemon() displays the name immediately and asks MediaService for the
// artwork asynchronously, showing a loading placeholder meanwhile. Because
// results arrive out of order when the user clicks quickly, the panel records
// the dex it currently wants and ignores any ready()/failed() for a different
// one (the stale-guard). On failure it shows a "no image" placeholder — the name
// still stands and nothing crashes.
//
// Opt-in copy mode: the 3-arg showPokemon() overload takes the species' owned copies.
// When non-empty, the panel picks one at random, shows compact condition / rarity / foil
// badges (each hidden when unset) plus a counter and the copy's comments, replaces the
// artwork with that copy's card image
// (falling back to the artwork when the copy has no saved image), and offers an
// "Edit card" button (editCopyRequested). Deliberately spare — set/number and ownership
// are omitted so the card image dominates the panel. It needs a CardImageStore for the copy image;
// when constructed without one, the copy widgets never appear and the plain 2-arg
// showPokemon() behaves exactly as before.
//
// Both the binder guide and the Pokémon browser use copy mode, but the copies they pass
// are scoped differently: the guide passes one binder's copies (listByBinder) so the
// counter reads "filed here"; the browser passes copies aggregated across every binder
// (listAll), where "filed here" would name no location — setCountedAcrossBinders(true)
// drops that suffix for the unscoped case.
class PokemonDetailPanel : public QWidget {
    Q_OBJECT

public:
    PokemonDetailPanel(MediaService& media, WishlistService& wishlist,
                       CardImageStore* images = nullptr, QWidget* parent = nullptr);

    // Show `name` now and request its artwork. Not named show() so it doesn't
    // hide QWidget::show().
    void showPokemon(int dexNumber, const QString& name);
    // As above, but with the species' owned copies filed in the current binder.
    // Non-empty enters copy mode (see the class docstring); empty is identical to
    // the 2-arg overload. `preferCopyId` names the copy to show — used to keep the
    // just-edited copy on screen when returning from the edit page; when empty (a
    // fresh row selection) one is picked at random.
    void showPokemon(int dexNumber, const QString& name,
                     const std::vector<CardCopy>& ownedCopiesHere,
                     const QString& preferCopyId = QString());
    // Empty state: no selection.
    void clear();

    // When true, the copy-count line drops the "filed here" suffix — for the Pokémon
    // browser, whose copy mode aggregates a species' copies across every binder, so no
    // single filing location exists. Defaults to false (binder-guide wording).
    void setCountedAcrossBinders(bool acrossBinders) { copiesAcrossBinders_ = acrossBinders; }

    // The id of the copy currently shown in copy mode, or "" when not in copy mode
    // (plain artwork / empty state). Lets an owning view re-show the SAME copy after
    // a rebuild (e.g. a header-sort refresh) instead of re-rolling a random one.
    QString shownCopyId() const { return shownCopyId_; }

Q_SIGNALS:
    // The user asked to add a card copy for the shown Pokémon. The panel is
    // embedded (in a splitter), so it can't host a full page itself — the owning
    // view turns this into a stack push to the AddCardCopyPage.
    void addCopyRequested(int dexNumber, const QString& name);
    // The user asked to edit the copy currently shown in copy mode. Carries the
    // copy's id; the owning view opens the EditCardCopyPage for it.
    void editCopyRequested(const QString& copyId);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void onReady(int dexNumber, MediaKind kind, const QPixmap& pixmap);
    void onFailed(int dexNumber, MediaKind kind);
    void onCardImageChanged(const QString& copyId);  // re-read the shown copy's image
    // Populate (and show) the copy-detail block for `copy`, out of `total` copies
    // filed here, and load its card image (fallback to artwork on a null pixmap).
    void showCopy(const CardCopy& copy, int total);
    // Show the copy's card scan (by copy id) when it has one, else fall back to
    // the Pokémon artwork. Shared by the initial show and the image-changed re-read.
    void showCopyImage(const std::string& copyId);
    // Hide the copy block, counter and edit button (plain / empty-state).
    void hideCopy();
    // Reset the image to the loading placeholder and (re)request the current
    // species' official artwork — the shared fallback when no copy scan is shown.
    void requestArtworkFallback();
    // Scale originalPixmap_ to fit the image label, or show placeholder_ text
    // when there is no pixmap (empty/loading/failed states).
    void renderImage();

    MediaService& media_;
    WishlistService& wishlist_;
    CardImageStore* images_;  // null in the Pokémon browser → copy mode disabled
    int currentDex_ = -1;  // the dex we currently want shown; guards stale results
    QString shownCopyId_;  // id of the copy shown in copy mode ("" = not in copy mode)
    bool showingCopyImage_ = false;  // the image is a copy scan, not artwork → keep
    bool copiesAcrossBinders_ = false;  // drop the counter's "filed here" (unscoped browser)
    QLabel* name_;
    QLabel* image_;
    QLabel* copyCondition_;  // compact condition badge (hidden when ungraded)
    QLabel* copyRarity_;     // compact rarity badge (hidden when unset)
    QLabel* copyFoil_;       // compact foil-treatment badge (hidden when unset)
    QLabel* copyCounter_;  // "N copies filed here"
    QLabel* copyComments_;
    QWidget* copyDetail_;  // container for the copy labels; hidden outside copy mode
    QPushButton* editButton_;
    QPushButton* addCopyButton_;
    WishlistSourcesEditor* wishlistEditor_;
    QPixmap originalPixmap_;  // full-resolution artwork; rescaled on resize
    QString placeholder_;     // text shown when there is no pixmap
};

}  // namespace pokedex
