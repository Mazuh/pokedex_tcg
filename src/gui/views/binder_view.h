#pragma once

#include <QWidget>

#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"

class QLineEdit;
class QTableWidget;

namespace pokedex {

class BinderGuideService;
class WishlistService;
class MediaService;
class PokemonDetailPanel;

// GUI — the "open binder" screen: the binder's guide as a list of its Pokémon,
// each paired with its CollectionStatus, above a live partial-name search box.
// A thin shell over BinderGuideService (the Qt-free verb). Read-only: it computes
// the entries once on construction and only re-filters the cached list as the
// user types — there are no per-entry actions in this slice.
//
// This is an embedded page, not a separate window: BindersWindow shows it inside
// a QStackedWidget and a "Back" button (which emits backRequested) returns to the
// binder list. Opening a binder is in-app navigation, not a modal detour.
class BinderView : public QWidget {
    Q_OBJECT

public:
    BinderView(BinderGuideService& guide, const CardBinder& binder,
               WishlistService& wishlist, MediaService& media, QWidget* parent = nullptr);

Q_SIGNALS:
    // Emitted when the user asks to leave this page (the Back button). The owner
    // navigates back to the binder list and disposes of this view.
    void backRequested();

private:
    // Show only the rows whose Pokémon name contains `filter` (case-insensitive);
    // an empty filter shows all. Rows are built once and toggled, not rebuilt.
    void applyFilter(const QString& filter);
    // Show the clicked/selected row's Pokémon in the detail panel. Reads the dex
    // number and name from the row's cells (columns 0 and 1).
    void showRow(int row);

    QTableWidget* table_;
    QLineEdit* search_;
    PokemonDetailPanel* detail_;
    std::vector<CardBinderEntry> entries_;
};

}  // namespace pokedex
