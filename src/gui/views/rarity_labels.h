#pragma once

#include <QString>

#include "core/domain/card_rarity.h"

namespace pokedex {

// GUI — human-facing label for a CardRarity. Kept out of the Qt-free core (like
// condition_labels.h / region_labels.h): display wording may diverge from — or be
// localized independently of — the storage token. The switch is exhaustive, so a
// new CardRarity fails -Wswitch under -Werror rather than rendering blank.
inline QString rarityLabel(CardRarity rarity) {
    switch (rarity) {
        case CardRarity::Common:                  return QStringLiteral("Common");
        case CardRarity::Uncommon:                return QStringLiteral("Uncommon");
        case CardRarity::Rare:                    return QStringLiteral("Rare");
        case CardRarity::DoubleRare:              return QStringLiteral("Double Rare");
        case CardRarity::IllustrationRare:        return QStringLiteral("Illustration Rare");
        case CardRarity::UltraRare:               return QStringLiteral("Ultra Rare");
        case CardRarity::SpecialIllustrationRare: return QStringLiteral("Special Illustration Rare");
        case CardRarity::HyperRare:               return QStringLiteral("Hyper Rare");
        case CardRarity::Promo:                   return QStringLiteral("Promo");
        case CardRarity::RareHolo:                return QStringLiteral("Rare Holo");
        case CardRarity::RareHoloEX:              return QStringLiteral("Rare Holo EX");
        case CardRarity::RarePrime:               return QStringLiteral("Rare Prime");
        case CardRarity::RareLegend:              return QStringLiteral("Rare LEGEND");
        case CardRarity::AmazingRare:             return QStringLiteral("Amazing Rare");
        case CardRarity::Shining:                 return QStringLiteral("Shining");
        case CardRarity::Radiant:                 return QStringLiteral("Radiant");
        case CardRarity::AceSpec:                 return QStringLiteral("ACE SPEC");
    }
    return QString();
}

// GUI — a one-sentence plain-language description of what a CardRarity means, for
// the "ⓘ/info" affordance next to the rarity picker (the terms read as opaque
// jargon otherwise). Where a rarity has a distinctive rarity symbol it is named in
// the text. Exhaustive switch so a new CardRarity fails -Wswitch under -Werror.
inline QString rarityDescription(CardRarity rarity) {
    switch (rarity) {
        case CardRarity::Common:
            return QStringLiteral(
                "Marked with a ● symbol. The most frequently pulled cards in booster "
                "packs — usually Basic Pokémon and simple Trainer cards.");
        case CardRarity::Uncommon:
            return QStringLiteral(
                "Marked with a ◆ symbol. Less common than Commons — often Stage 1 "
                "Pokémon and stronger Trainer cards.");
        case CardRarity::Rare:
            return QStringLiteral(
                "Marked with a ★ symbol. A standard rare card; may be Non-Holo or Holo "
                "depending on the set.");
        case CardRarity::DoubleRare:
            return QStringLiteral(
                "Marked with ★★ (abbreviated RR). Typically Pokémon ex or equivalent "
                "cards — harder to pull than a Rare.");
        case CardRarity::IllustrationRare:
            return QStringLiteral(
                "Abbreviated IR. A full-art illustration focused on the artwork rather "
                "than the gameplay layout.");
        case CardRarity::UltraRare:
            return QStringLiteral(
                "Marked with ★★ (abbreviated UR). Full Art Pokémon, Supporters, or "
                "Special Energy cards — usually textured.");
        case CardRarity::SpecialIllustrationRare:
            return QStringLiteral(
                "Abbreviated SIR. A premium full-art card with highly detailed artwork "
                "— among the chase cards of a set.");
        case CardRarity::HyperRare:
            return QStringLiteral(
                "Marked with ★★★ (abbreviated HR). Gold cards with textured foil, "
                "usually among the rarest cards in a regular expansion.");
        case CardRarity::Promo:
            return QStringLiteral(
                "Distributed through products, events, or promotions instead of booster "
                "packs.");
        case CardRarity::RareHolo:
            return QStringLiteral("Legacy: a standard holographic rare card.");
        case CardRarity::RareHoloEX:
            return QStringLiteral("Legacy: Pokémon-EX cards from the EX era.");
        case CardRarity::RarePrime:
            return QStringLiteral(
                "Legacy: Prime Pokémon from the HeartGold & SoulSilver era.");
        case CardRarity::RareLegend:
            return QStringLiteral("Legacy: two-card LEGEND Pokémon.");
        case CardRarity::AmazingRare:
            return QStringLiteral(
                "Legacy: cards with rainbow burst backgrounds and unique typing.");
        case CardRarity::Shining:
            return QStringLiteral(
                "Legacy: special shiny Pokémon cards from selected expansions.");
        case CardRarity::Radiant:
            return QStringLiteral(
                "Legacy: shiny Pokémon with unique gameplay restrictions.");
        case CardRarity::AceSpec:
            return QStringLiteral(
                "Legacy: powerful Trainer cards limited to one copy per deck.");
    }
    return QString();
}

}  // namespace pokedex
