#pragma once

#include <QWidget>

#include <array>
#include <span>

#include "core/app/pokemon_browse_service.h"
#include "core/domain/region.h"

class QLabel;

namespace pokedex {

class CardCopyService;
// The hand-painted progress strip, defined in the .cpp beside the row it also fills.
class BarStrip;

// GUI — the "Gotta Catch 'Em All!" screen: how far the collection has come, as a
// whole-collection headline over a global progress bar and a per-region breakdown
// (one row per region with a bar, "95 of 151" and a percentage).
//
// This was a header ABOVE the Pokémon browser's search box; nine rows of it cost
// more vertical space than a list can spare, so it became a page of its own —
// pushed onto PokemonListView's inner stack by the button beside that search box,
// with a Back top bar (the WishlistEditPage idiom). A page rather than a sidebar
// section because these figures are *about* the browse list: they read the same
// species catalog, and every row on them is a way into it. The move is what lets
// the figures breathe (a real global bar, wider region bars) — but it is also why
// the screen no longer highlights "the region currently being shown": there is no
// list beside it to be showing anything.
//
// The figures are ABSOLUTE — the whole collection against the whole catalog. They
// are read from PokemonBrowseService on every (re-)show, gated on the copy
// revision so an unchanged collection costs nothing.
//
// Clicking a region row asks the host to browse that region (regionActivated); the
// host pops this page and narrows its search box, so filtering stays the one path
// it already was. A row responds to the mouse only: it is not tab-focusable and has
// no keyboard activation, matching the rest of the app's non-table affordances.
class CaptureProgressView : public QWidget {
    Q_OBJECT

public:
    CaptureProgressView(PokemonBrowseService& browse, CardCopyService& copies,
                        QWidget* parent = nullptr);

signals:
    // The user clicked a region row, asking to browse only that region. The host owns
    // both closing this page and the search box that does the narrowing.
    void regionActivated(Region region);

    // The Back button: the host pops + disposes of this page (nothing is edited here,
    // so there is no save step and no discard prompt).
    void backRequested();

protected:
    // Re-read the collection each time this section becomes visible, so a copy added
    // or removed in another section moves the figures (mirrors the other sections'
    // showEvent reload). Gated on CardCopyService::revision().
    void showEvent(QShowEvent* event) override;

private:
    // One region's row widgets, built once in the constructor (the regions are
    // compile-time constant, so a refresh only re-texts them).
    struct Row;

    // Re-query the browse list and re-render, unless the inventory hasn't moved since
    // the last successful read.
    void refresh();
    // Render the figures: the per-region rows and, folded from them (totalProgress),
    // the headline and the global bar. Expects the WHOLE region array from
    // regionProgress.
    void setProgress(std::span<const RegionProgress> regions);

    PokemonBrowseService& browse_;
    CardCopyService& copies_;
    QLabel* capturedStat_;
    QLabel* cardsStat_;
    QLabel* errorLabel_;
    QWidget* body_;
    BarStrip* totalBar_;
    std::array<Row*, kRegions.size()> rows_;
    // CardCopyService::revision() at the last SUCCESSFUL read, so a re-show of an
    // unchanged collection skips the query. Held at the -1 sentinel after a failed
    // read (never a real revision), so the next show retries rather than latching
    // the empty figures the failure would otherwise leave on screen.
    long revision_ = -1;
};

}  // namespace pokedex
