#pragma once

#include <QString>

#include "core/domain/card_foil.h"

namespace pokedex {

// GUI — human-facing label for a CardFoil (foil treatment / finish). Kept out of
// the Qt-free core (like condition_labels.h): display wording may diverge from the
// storage token. Exhaustive switch, so a new CardFoil fails -Wswitch under -Werror
// rather than rendering blank.
inline QString foilLabel(CardFoil foil) {
    switch (foil) {
        case CardFoil::NonHolo:        return QStringLiteral("Non-Holo");
        case CardFoil::Holo:           return QStringLiteral("Holo");
        case CardFoil::ReverseHolo:    return QStringLiteral("Reverse Holo");
        case CardFoil::CosmosHolo:     return QStringLiteral("Cosmos Holo");
        case CardFoil::MirrorHolo:     return QStringLiteral("Mirror Holo");
        case CardFoil::CrackedIceHolo: return QStringLiteral("Cracked Ice Holo");
        case CardFoil::ConfettiHolo:   return QStringLiteral("Confetti Holo");
        case CardFoil::CrosshatchHolo: return QStringLiteral("Crosshatch Holo");
        case CardFoil::HDHolo:         return QStringLiteral("HD Holo");
        case CardFoil::Textured:       return QStringLiteral("Textured");
    }
    return QString();
}

// GUI — a one-sentence plain-language description of a CardFoil, for the "ⓘ/info"
// affordance next to the foil picker. Exhaustive switch so a new CardFoil fails
// -Wswitch under -Werror rather than rendering blank.
inline QString foilDescription(CardFoil foil) {
    switch (foil) {
        case CardFoil::NonHolo:
            return QStringLiteral("No holographic effect anywhere on the card.");
        case CardFoil::Holo:
            return QStringLiteral("Only the artwork window is holographic.");
        case CardFoil::ReverseHolo:
            return QStringLiteral(
                "The entire card is holographic except for the artwork window.");
        case CardFoil::CosmosHolo:
            return QStringLiteral(
                "A circular \"orb\" holographic pattern, commonly found on promotional "
                "cards.");
        case CardFoil::MirrorHolo:
            return QStringLiteral(
                "Uniform reflective foil covering most of the card — mostly found in "
                "Asian releases.");
        case CardFoil::CrackedIceHolo:
            return QStringLiteral(
                "A foil pattern resembling cracked ice, common on older promotional "
                "cards.");
        case CardFoil::ConfettiHolo:
            return QStringLiteral("Foil made up of tiny reflective dots or particles.");
        case CardFoil::CrosshatchHolo:
            return QStringLiteral(
                "A foil pattern with a crosshatched or grid-like appearance.");
        case CardFoil::HDHolo:
            return QStringLiteral(
                "A high-definition holographic pattern used in some modern products.");
        case CardFoil::Textured:
            return QStringLiteral(
                "Raised surface texture combined with holographic foil — common on "
                "Ultra Rares, SIRs, and Hyper Rares.");
    }
    return QString();
}

}  // namespace pokedex
