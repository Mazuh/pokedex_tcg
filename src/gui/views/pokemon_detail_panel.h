#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

#include "core/app/pokemon_external_api.h"

class QLabel;

namespace pokedex {

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
class PokemonDetailPanel : public QWidget {
    Q_OBJECT

public:
    PokemonDetailPanel(MediaService& media, WishlistService& wishlist,
                       QWidget* parent = nullptr);

    // Show `name` now and request its artwork. Not named show() so it doesn't
    // hide QWidget::show().
    void showPokemon(int dexNumber, const QString& name);
    // Empty state: no selection.
    void clear();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void onReady(int dexNumber, MediaKind kind, const QPixmap& pixmap);
    void onFailed(int dexNumber, MediaKind kind);
    // Scale originalPixmap_ to fit the image label, or show placeholder_ text
    // when there is no pixmap (empty/loading/failed states).
    void renderImage();

    MediaService& media_;
    WishlistService& wishlist_;
    int currentDex_ = -1;  // the dex we currently want shown; guards stale results
    QLabel* name_;
    QLabel* image_;
    WishlistSourcesEditor* wishlistEditor_;
    QPixmap originalPixmap_;  // full-resolution artwork; rescaled on resize
    QString placeholder_;     // text shown when there is no pixmap
};

}  // namespace pokedex
