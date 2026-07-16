#pragma once

#include <QWidget>

class QLineEdit;
class QVBoxLayout;

namespace pokedex {

class WishlistService;

// GUI — the per-Pokémon wishlist editor: the list of a species' sources (seller
// names or marketplace links) with add / edit / remove, shown below the artwork
// in PokemonDetailPanel. A thin shell over WishlistService (the Qt-free verbs):
// each action calls the service and reloads the list from it, so the display is
// always the stored truth. URL sources render as clickable links (source_label.h).
//
// setPokemon() points it at a species and loads that species' sources;
// clear() empties it for the no-selection state. Because it edits the source of
// truth, it is the one interactive part of the otherwise read-only detail panel.
class WishlistSourcesEditor : public QWidget {
    Q_OBJECT

public:
    explicit WishlistSourcesEditor(WishlistService& wishlist, QWidget* parent = nullptr);

    // Load and show the sources for `dexNumber` (creating nothing until the user
    // adds one). Enables the add row.
    void setPokemon(int dexNumber);
    // Empty state: no species selected, add row disabled.
    void clear();

private:
    // Rebuild the source rows for currentDex_ from the service (never appends —
    // the set is small and always redrawn after a change).
    void reload();
    // Add whatever is in the input line, if non-blank; clears the input on success.
    void addFromInput();

    WishlistService& wishlist_;
    int currentDex_ = -1;  // -1 when nothing is selected

    QVBoxLayout* rows_;  // one entry widget per source
    QLineEdit* input_;
};

}  // namespace pokedex
