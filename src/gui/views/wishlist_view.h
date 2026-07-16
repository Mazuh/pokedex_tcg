#pragma once

#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

namespace pokedex {

class WishlistService;

// GUI — the Wishlist section of the main window: the whole wishlist unscoped, as
// a flat table with one row per source (a Pokémon with three sources shows as
// three rows), so it reads like a shopping list to build a cart from. A thin
// shell over WishlistService (the Qt-free verbs): it (re)reads the list on every
// change rather than tracking incremental edits — the wishlist is small.
//
// Full CRUD lives here: Add… (pick a species + type a source), Edit… and Delete
// on the selected row. URL sources render as clickable links (source_label.h).
// The complementary per-Pokémon editor is WishlistSourcesEditor, shown in the
// detail panel of the Pokémon and binder pages; the two edit the same data.
class WishlistView : public QWidget {
    Q_OBJECT

public:
    explicit WishlistView(WishlistService& wishlist, QWidget* parent = nullptr);

protected:
    // Re-read the wishlist each time this section is shown, so edits made from a
    // Pokémon detail panel's editor (which mutate the same WishlistService) appear
    // when the user switches back to this section.
    void showEvent(QShowEvent* event) override;

private:
    // Rebuild the table from WishlistService::listAll(), one row per source.
    void refresh();
    // Add…: prompt for a species and a source, then add it.
    void addEntry();
    // Edit… / Delete on the selected row (a no-op when nothing is selected).
    void editSelected();
    void removeSelected();
    void updateButtonState();

    WishlistService& wishlist_;
    QTableWidget* table_;
    QLabel* emptyLabel_;
    QPushButton* editButton_;
    QPushButton* removeButton_;
};

}  // namespace pokedex
