#pragma once

#include <QString>
#include <QWidget>

namespace pokedex {

class WishlistService;

// GUI — the per-Pokémon wishlist screen: a full in-window page dedicated to one
// species' wishlist sources (seller names or marketplace links), with add / edit /
// remove. It hosts the reusable WishlistSourcesEditor under a Back top bar and a
// species heading, mirroring the Add/Edit card-copy pages.
//
// It exists so the detail panel doesn't have to embed the whole sources CRUD below
// the artwork (which crowded the card info): the panel now shows a compact
// "Wishlist (N)" button that pushes this page. Like AddCardCopyPage, it is pushed
// onto a host's QStackedWidget (PokemonListView or BinderView) and its Back button
// emits backRequested() so the host pops + disposes of it. All edits write straight
// through WishlistService, so no "changed" signal is needed — the host re-reads on
// return (the detail panel's counter, and the binder guide's CollectionStatus, both
// recompute from the stored truth).
class WishlistEditPage : public QWidget {
    Q_OBJECT

public:
    // `wishlist` must outlive this page. `dexNumber` is the species whose wishlist
    // is edited; `speciesName` fills the heading.
    WishlistEditPage(WishlistService& wishlist, int dexNumber, const QString& speciesName,
                     QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();
};

}  // namespace pokedex
