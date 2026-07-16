#pragma once

#include <QTableWidgetItem>

namespace pokedex {

// GUI — a non-editable table cell holding `text`. The read-only table views
// (binders list, binder guide, Pokémon browser) all build their cells this way;
// this is the single place that spelling lives, kept header-only alongside the
// other GUI helpers (region_labels.h, status_labels.h). Empty text renders as an
// em-dash so missing data (e.g. a binder with no region) reads as "none" rather
// than a blank gap — the single place this convention lives.
inline QTableWidgetItem* cell(const QString& text) {
    auto* item = new QTableWidgetItem(text.isEmpty() ? QStringLiteral("—") : text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

}  // namespace pokedex
