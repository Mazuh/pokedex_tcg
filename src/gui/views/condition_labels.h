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

// GUI — a one-sentence plain-language description of what a CardCondition means,
// for an "?/info" affordance next to the condition picker (the grades read as opaque
// jargon otherwise). Exhaustive switch so a new CardCondition fails -Wswitch under
// -Werror rather than rendering blank. Wording tracks the standard TCG grade
// definitions, trimmed to the five grades this app records.
inline QString conditionDescription(CardCondition condition) {
    switch (condition) {
        case CardCondition::NearMint:
            return QStringLiteral(
                "Excellent condition with only minimal signs of handling — tiny edge "
                "whitening or a very light surface scratch may be present. The standard "
                "condition for most collectible cards.");
        case CardCondition::LightlyPlayed:
            return QStringLiteral(
                "Minor wear from regular play — small scratches, edge whitening, or slight "
                "corner wear are visible, but the card still looks attractive.");
        case CardCondition::ModeratelyPlayed:
            return QStringLiteral(
                "Clearly played — noticeable whitening, scratches, multiple worn corners, "
                "or minor dents. Still tournament legal if otherwise undamaged.");
        case CardCondition::HeavilyPlayed:
            return QStringLiteral(
                "Significant wear — heavy whitening, creases, dirt, dents, or surface "
                "damage throughout. Card integrity remains mostly intact.");
        case CardCondition::Damaged:
            return QStringLiteral(
                "Severe damage such as major creases, tears, water damage, ink marks, "
                "peeling, or bends. Primarily valued as a binder filler or for gameplay.");
    }
    return QString();
}

// GUI — the bare TCG abbreviation for a CardCondition (NM, LP, MP, HP, Dmg),
// for tight table columns where the full label doesn't fit. Exhaustive switch
// so a new CardCondition fails -Wswitch under -Werror rather than rendering
// blank.
inline QString conditionAbbrev(CardCondition condition) {
    switch (condition) {
        case CardCondition::NearMint:         return QStringLiteral("NM");
        case CardCondition::LightlyPlayed:    return QStringLiteral("LP");
        case CardCondition::ModeratelyPlayed: return QStringLiteral("MP");
        case CardCondition::HeavilyPlayed:    return QStringLiteral("HP");
        case CardCondition::Damaged:          return QStringLiteral("Dmg");
    }
    return QString();
}

}  // namespace pokedex
