#pragma once

#include <QTableWidgetItem>

namespace pokedex {

// GUI — a non-editable table cell holding `text`. The read-only table views
// (binders list, binder guide, Pokémon browser) all build their cells this way;
// this is the single place that spelling lives, kept header-only alongside the
// other GUI helpers (region_labels.h, status_labels.h).
inline QTableWidgetItem* cell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

}  // namespace pokedex
