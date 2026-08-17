#pragma once

#include <QTableWidgetItem>

#include "gui/views/tooltip_text.h"

namespace pokedex {

// GUI — a non-editable table cell holding `text`. The read-only table views
// (binders list, binder guide, Pokémon browser) all build their cells this way;
// this is the single place that spelling lives, kept header-only alongside the
// other GUI helpers (region_labels.h, status_labels.h). Empty text renders as an
// em-dash so missing data (e.g. a binder with no region) reads as "none" rather
// than a blank gap — the single place this convention lives.
//
// Every cell also carries its own text as its tooltip — the HTML `title` idiom.
// Columns elide to "…" whenever they are narrower than their content (a Stretch
// column under a squeezed window, a ResizeToContents one the user dragged in), and
// a truncated cell is otherwise unreadable with no way to see the rest. Doing it
// here rather than per view means no table can forget: a new column is covered the
// moment it is built through cell(). It is unconditional rather than measured
// against the column width — Qt has no "is this item elided" signal, and a tooltip
// that merely repeats what is already on screen costs nothing. An empty cell gets
// none: the em-dash placeholder has nothing to reveal.
inline QTableWidgetItem* cell(const QString& text) {
    auto* item = new QTableWidgetItem(text.isEmpty() ? QStringLiteral("—") : text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    if (!text.isEmpty()) {
        item->setToolTip(tooltipText(text));  // free text: never let it read as markup
    }
    return item;
}

// GUI — add `extra` to a cell's tooltip, keeping whatever it already carries (its own
// text, per cell() above) ahead of it, blank-line separated. A cell with something
// more to say than its contents (the binder guide's "moved here by hand" note on a
// hand-placed row's Page/Pocket) uses this rather than setToolTip, which would drop
// the text the tooltip exists to reveal in the first place.
inline void addToolTip(QTableWidgetItem* item, const QString& extra) {
    if (extra.isEmpty()) {
        return;
    }
    const QString existing = item->toolTip();
    if (existing.isEmpty()) {
        item->setToolTip(tooltipText(extra));
        return;
    }
    // cell() already made the existing tooltip safe; the separator has to match the text
    // path it landed on, since a rich-text tooltip collapses a literal newline.
    const QString separator = Qt::mightBeRichText(existing) ? QStringLiteral("<br><br>")
                                                            : QStringLiteral("\n\n");
    item->setToolTip(existing + separator + tooltipText(extra));
}

}  // namespace pokedex
