#pragma once

#include <QWidget>

#include <array>
#include <optional>
#include <span>

#include "core/app/pokemon_browse_service.h"
#include "core/domain/region.h"

class QLabel;
class QToolButton;

namespace pokedex {

// GUI — the Pokémon section's progress header: a muted whole-collection line
// ("Captured 341 of 1025 (33%) · Cards 512") over a collapsible per-region
// breakdown, one row per region with a bar, "95 of 151" and a percentage.
//
// The figures it shows are ABSOLUTE — the whole collection against the whole
// catalog — and never scoped to the search below it. The list's own "Showing N of
// M" label is what describes the filtered view; a header that shrank with the
// filter would report a complete-looking "95 of 95" for whatever is on screen.
//
// Clicking a region row asks the host to show only that region (regionActivated);
// the host does that through its existing search box, so there is one filtering
// path rather than two. The panel therefore keeps NO active-region state of its
// own — the host tells it what to highlight via setActiveRegion, derived from the
// box, so typing or clearing the search by hand keeps the highlight honest.
//
// A row responds to the mouse only: it is not tab-focusable and has no keyboard
// activation, matching the rest of the app's non-table affordances.
class RegionProgressPanel : public QWidget {
    Q_OBJECT

public:
    explicit RegionProgressPanel(QWidget* parent = nullptr);

    // Render the figures: the per-region rows and, folded from them (totalProgress),
    // the headline. Touches only text and bar ratios — never the highlight — so it
    // can't clobber setActiveRegion, which runs on a different trigger (a refresh
    // versus every keystroke). Expects the WHOLE region array from regionProgress.
    void setProgress(std::span<const RegionProgress> regions);

    // Highlight the region currently being shown alone, or none. The complement of
    // setProgress: touches only the highlight, never the figures.
    void setActiveRegion(std::optional<Region> region);

signals:
    // The user clicked a region row, asking to see only that region — or, when it is
    // already the active one, to stop filtering. The host decides which, since it owns
    // the search box that says what is currently shown.
    void regionActivated(Region region);

private:
    // One region's row widgets, built once in the constructor (the regions are
    // compile-time constant, so a refresh only re-texts them).
    struct Row;

    QLabel* capturedStat_;
    QLabel* cardsStat_;
    QToolButton* toggle_;
    QWidget* rowsBox_;
    std::array<Row*, kRegions.size()> rows_;
};

}  // namespace pokedex
