#pragma once

#include <QCoreApplication>
#include <QString>

#include <optional>

#include "core/domain/card_binder.h"

namespace pokedex {

// GUI — display strings for a binder's physical layout (its pocket grid and where a
// card sits in it), kept out of the Qt-free core alongside the other label helpers
// (region_labels.h, status_labels.h). Shared by the binders list, which shows a
// binder's grid, and the binder guide, which pages by it — so the "rows×columns"
// spelling lives in exactly one place.

// A pocket's position within its page as "row×column", from a 0-based index counted
// the way the page is read: left to right, then top to bottom. "2×3" is the second
// row, third pocket across. `columns` must be positive (a caller with no grid has
// nothing to label).
inline QString pocketLabel(int indexInPage, int columns) {
    return QStringLiteral("%1×%2").arg(indexInPage / columns + 1).arg(indexInPage % columns + 1);
}

// A binder's grid as "3×3", or empty when it isn't recorded.
inline QString pocketGridLabel(const std::optional<CardBinderPocketGrid>& grid) {
    if (!grid) {
        return QString();
    }
    return QStringLiteral("%1×%2").arg(grid->rows).arg(grid->columns);
}

// A count against a whole, as a percentage — "<1%" and ">99%" guard the rounding
// extremes so the figure can never contradict the counts beside it (a tiny nonzero
// ratio must not read as 0%, and a nearly-complete one must not read as a false 100%).
//
// It deliberately does NOT clamp at 100: a binder holding more cards than its stated
// capacity is a real thing the app never blocks, so an over-full album honestly reads
// 115% rather than pretending to be exactly full. Callers must ensure `whole > 0`.
inline QString percentLabel(int part, int whole) {
    const int percent = qRound(100.0 * part / whole);
    if (percent == 0 && part > 0) {
        return QCoreApplication::translate("pokedex", "<1%");
    }
    if (percent == 100 && part < whole) {
        return QCoreApplication::translate("pokedex", ">99%");
    }
    return QCoreApplication::translate("pokedex", "%1%").arg(percent);
}

}  // namespace pokedex
