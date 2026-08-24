#pragma once

#include <QString>

#include "core/domain/card_foil.h"

namespace pokedex {

// GUI — human-facing label for a CardFoil (foil treatment / finish). Kept out of the
// Qt-free core (like condition_labels.h): display wording may diverge from the storage
// token, and here it deliberately does (the token is the enumerator's spelling and can
// never change; the label is free). Exhaustive switch, so a new CardFoil fails -Wswitch
// under -Werror rather than rendering blank. Every value has a label, including the
// RETIRED ones — a copy already recorded with one still shows it in the card tables,
// only the picker withholds it.
inline QString foilLabel(CardFoil foil) {
    switch (foil) {
        case CardFoil::NonHolo:          return QStringLiteral("Non-Holo");
        case CardFoil::Holo:             return QStringLiteral("Standard Holo");
        case CardFoil::ReverseHolo:      return QStringLiteral("Reverse Holo");
        case CardFoil::FullCardHolo:     return QStringLiteral("Full-Card Holo");
        case CardFoil::Textured:         return QStringLiteral("Textured Holo");
        case CardFoil::CosmosHolo:       return QStringLiteral("Cosmos Holo");
        case CardFoil::WaterWebHolo:     return QStringLiteral("Water Web Holo");
        case CardFoil::VerticalLineHolo: return QStringLiteral("Vertical Line Holo");
        case CardFoil::MirageHolo:       return QStringLiteral("Mirage Holo");
        case CardFoil::CrackedIceHolo:   return QStringLiteral("Cracked Ice Holo");
        case CardFoil::ConfettiHolo:     return QStringLiteral("Confetti Holo");
        case CardFoil::CrosshatchHolo:   return QStringLiteral("Crosshatch Holo");
        case CardFoil::MirrorHolo:       return QStringLiteral("Mirror Holo");
        case CardFoil::OtherFoil:        return QStringLiteral("Other / Unknown");
        case CardFoil::HDHolo:           return QStringLiteral("HD Holo");
    }
    return QString();
}

// GUI — a one-sentence plain-language description of a CardFoil, for the "ⓘ/info"
// affordance next to the foil picker. Exhaustive switch so a new CardFoil fails
// -Wswitch under -Werror rather than rendering blank.
inline QString foilDescription(CardFoil foil) {
    switch (foil) {
        case CardFoil::NonHolo:
            return QStringLiteral("No holographic foil anywhere on the card.");
        case CardFoil::Holo:
            return QStringLiteral(
                "The traditional holo treatment, concentrated in the artwork area.");
        case CardFoil::ReverseHolo:
            return QStringLiteral(
                "Foil everywhere except the artwork area — the reverse of a standard "
                "holo.");
        case CardFoil::FullCardHolo:
            return QStringLiteral("Foil extending across most or all of the card.");
        case CardFoil::Textured:
            return QStringLiteral(
                "A holographic card with an embossed, ridged surface texture — common on "
                "Ultra Rares, SIRs, and Hyper Rares.");
        case CardFoil::CosmosHolo:
            return QStringLiteral(
                "A circular, star-like \"galaxy\" foil pattern, commonly found on "
                "promotional cards.");
        case CardFoil::WaterWebHolo:
            return QStringLiteral(
                "A flowing, wavy foil pattern, used heavily in the Sun & Moon era.");
        case CardFoil::VerticalLineHolo:
            return QStringLiteral(
                "A vertical-line foil pattern associated with Sword & Shield-era holos.");
        case CardFoil::MirageHolo:
            return QStringLiteral(
                "A modern horizontal, refraction-style foil pattern.");
        case CardFoil::CrackedIceHolo:
            return QStringLiteral(
                "A shattered, cracked-glass appearance, common on older promotional "
                "cards.");
        case CardFoil::ConfettiHolo:
            return QStringLiteral("Small reflective confetti-like flecks across the foil.");
        case CardFoil::CrosshatchHolo:
            return QStringLiteral("A grid-like, crosshatched foil pattern.");
        case CardFoil::MirrorHolo:
            return QStringLiteral(
                "Highly reflective, mirror-like foil — mostly found in Asian releases.");
        case CardFoil::OtherFoil:
            return QStringLiteral(
                "An unusual or unidentified treatment. Not the same as leaving this "
                "blank: blank means you haven't recorded it yet, this means you have "
                "looked and it is none of the above.");
        case CardFoil::HDHolo:
            return QStringLiteral(
                "No longer offered: \"high definition\" was never a distinct printed "
                "treatment.");
    }
    return QString();
}

}  // namespace pokedex
