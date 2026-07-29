#pragma once

#include <QString>
#include <QStringList>

#include <vector>

#include "core/domain/region.h"

namespace pokedex {

// GUI — human-facing region names for the binder views. Kept separate from the
// storage tokens in core/storage/codecs (persistence format), since a display
// label may diverge from its on-disk token (e.g. if later localized). The set of
// regions comes from the canonical pokedex::kRegions, so the picker never drifts
// from the enum.

inline QString regionLabel(Region region) {
    switch (region) {
        case Region::Kanto:  return QStringLiteral("Kanto");
        case Region::Johto:  return QStringLiteral("Johto");
        case Region::Hoenn:  return QStringLiteral("Hoenn");
        case Region::Sinnoh: return QStringLiteral("Sinnoh");
        case Region::Unova:  return QStringLiteral("Unova");
        case Region::Kalos:  return QStringLiteral("Kalos");
        case Region::Alola:  return QStringLiteral("Alola");
        case Region::Galar:  return QStringLiteral("Galar");
        case Region::Paldea: return QStringLiteral("Paldea");
    }
    return QString();
}

// A binder's region set as one comma-joined label ("Kanto, Johto"), in the order
// given (repositories keep it canonical). Empty for a region-less binder, which
// the table cells then render as an em-dash. Shared by the binders table, the
// binder combo, and My Cards so a multi-region binder reads the same everywhere.
inline QString regionsLabel(const std::vector<Region>& regions) {
    QStringList parts;
    for (const Region region : regions) {
        parts << regionLabel(region);
    }
    return parts.join(QStringLiteral(", "));
}

}  // namespace pokedex
