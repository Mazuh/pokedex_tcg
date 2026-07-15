#pragma once

#include <QString>

#include "core/domain/collection_status.h"

namespace pokedex {

// GUI — human-facing labels for a Pokémon's CollectionStatus within a binder.
// Kept out of the Qt-free core (like region_labels.h): the display wording may
// later diverge from — or be localized independently of — the enum. The switch
// is exhaustive, so a new CollectionStatus value fails -Wswitch under -Werror
// rather than silently rendering blank.
inline QString statusLabel(CollectionStatus status) {
    switch (status) {
        case CollectionStatus::Incoming:   return QStringLiteral("Incoming");
        case CollectionStatus::Completed:  return QStringLiteral("Completed");
        case CollectionStatus::Wished:     return QStringLiteral("Wished");
        case CollectionStatus::Elsewhere:  return QStringLiteral("Elsewhere");
        case CollectionStatus::Removed:    return QStringLiteral("Removed");
        case CollectionStatus::Incomplete: return QStringLiteral("Incomplete");
    }
    return QString();
}

}  // namespace pokedex
