#pragma once

#include <QString>

#include "core/domain/card_condition.h"

namespace pokedex {

// GUI — human-facing labels for a CardCondition, with the standard TCG
// abbreviation. Kept out of the Qt-free core (like region_labels.h /
// status_labels.h): display wording may diverge from — or be localized
// independently of — the storage token. The switch is exhaustive, so a new
// CardCondition fails -Wswitch under -Werror rather than rendering blank.
inline QString conditionLabel(CardCondition condition) {
    switch (condition) {
        case CardCondition::NearMint:         return QStringLiteral("Near Mint (NM)");
        case CardCondition::LightlyPlayed:    return QStringLiteral("Lightly Played (LP)");
        case CardCondition::ModeratelyPlayed: return QStringLiteral("Moderately Played (MP)");
        case CardCondition::HeavilyPlayed:    return QStringLiteral("Heavily Played (HP)");
        case CardCondition::Damaged:          return QStringLiteral("Damaged");
    }
    return QString();
}

}  // namespace pokedex
