#pragma once

#include <QWidget>

#include <vector>

#include "core/app/pokemon_browse_service.h"

class QLabel;
class QLineEdit;
class QTableWidget;

namespace pokedex {

class MediaService;
class PokemonDetailPanel;

// GUI — the Pokémon section of the main window: an unscoped, read-only browse of
// the whole National Pokédex (# / name / region / owned-copy count) with a live
// partial-name search. A thin shell over PokemonBrowseService (the Qt-free
// verb): it computes the entries once on construction, then filters and paginates
// the cached list. Read-only in this slice — there are no per-Pokémon actions.
//
// The full catalog is ~1000 species, so the table loads incrementally
// (infinite scroll): it starts with one chunk of rows and appends the next
// chunk as the user scrolls near the bottom, keeping the initial render cheap
// and the view free of a 1025-row wall. It is an embedded section in
// MainWindow's stack, not a separate window.
class PokemonListView : public QWidget {
    Q_OBJECT

public:
    PokemonListView(PokemonBrowseService& service, MediaService& media,
                    QWidget* parent = nullptr);

protected:
    // Watches the table viewport's resize so the list keeps filling a viewport
    // that grew taller than the loaded rows (where no scrollbar exists yet, so
    // scrolling can't drive the next load).
    bool eventFilter(QObject* watched, QEvent* event) override;

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
    // the filtered_-index indirection.
    void showRow(int row);

    PokemonBrowseService& service_;
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
    // Guards fillViewport() against re-entry: appending rows can make a
    // scrollbar appear, which resizes the viewport and re-fires the resize
    // event mid-fill — without this it would overshoot the rows actually needed.
    bool filling_ = false;
};

}  // namespace pokedex
