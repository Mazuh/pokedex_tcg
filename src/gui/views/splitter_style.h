#pragma once

#include <QSplitter>

namespace pokedex {

// GUI — style a splitter's drag handle as a crisp 1px divider line rather than
// the native centered grip (which, unstyled, renders as a faint "dot" that reads
// as a smudge). The `palette(mid)` fill adapts to light/dark. All three
// splitters (the window's sidebar split, and the list⇄detail splits in the
// Pokémon browser and binder guide) go through this one place, kept header-only
// alongside the other GUI helpers (table_cell.h, region_labels.h).
inline void thinDivider(QSplitter* splitter) {
    splitter->setHandleWidth(1);
    splitter->setStyleSheet("QSplitter::handle { background: palette(mid); }");
}

}  // namespace pokedex
