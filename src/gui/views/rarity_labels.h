#pragma once

#include <QString>

#include "core/domain/card_rarity.h"

namespace pokedex {

// GUI — human-facing label for a CardRarity. Kept out of the Qt-free core (like
// condition_labels.h / region_labels.h): display wording may diverge from — or be
// localized independently of — the storage token, and here it deliberately does (the
// token is the enumerator's spelling and can never change; the label is free). The
// switch is exhaustive, so a new CardRarity fails -Wswitch under -Werror rather than
// rendering blank. Every value has a label, including the RETIRED ones — a copy already
// recorded with one still shows it in the card tables, only the picker withholds it.
inline QString rarityLabel(CardRarity rarity) {
    switch (rarity) {
        case CardRarity::Common:                  return QStringLiteral("Common");
        case CardRarity::Uncommon:                return QStringLiteral("Uncommon");
        case CardRarity::Rare:                    return QStringLiteral("Rare");
        case CardRarity::DoubleRare:              return QStringLiteral("Double Rare");
        case CardRarity::UltraRare:               return QStringLiteral("Ultra Rare");
        case CardRarity::IllustrationRare:        return QStringLiteral("Illustration Rare");
        case CardRarity::SpecialIllustrationRare: return QStringLiteral("Special Illustration Rare");
        case CardRarity::MegaHyperRare:           return QStringLiteral("Mega Hyper Rare");
        case CardRarity::AceSpec:                 return QStringLiteral("ACE SPEC Rare");
        case CardRarity::ShinyRare:               return QStringLiteral("Shiny Rare");
        case CardRarity::ShinyUltraRare:          return QStringLiteral("Shiny Ultra Rare");
        case CardRarity::HyperRare:               return QStringLiteral("Hyper Rare");
        case CardRarity::BlackWhiteRare:          return QStringLiteral("Black White Rare");
        case CardRarity::MegaAttackRare:          return QStringLiteral("Mega Attack Rare");
        case CardRarity::FuturisticRare:          return QStringLiteral("Futuristic Rare");
        case CardRarity::RareHolo:                return QStringLiteral("Rare Holo");
        case CardRarity::RareHoloEX:              return QStringLiteral("Rare Holo EX");
        case CardRarity::RareHoloGX:              return QStringLiteral("Rare Holo GX");
        case CardRarity::RareHoloLvX:             return QStringLiteral("Rare Holo Lv.X");
        case CardRarity::RarePrime:               return QStringLiteral("Rare Prime");
        case CardRarity::RareLegend:              return QStringLiteral("LEGEND");
        case CardRarity::RareBreak:               return QStringLiteral("Rare BREAK");
        case CardRarity::HoloRareV:               return QStringLiteral("Holo Rare V");
        case CardRarity::HoloRareVMAX:            return QStringLiteral("Holo Rare VMAX");
        case CardRarity::HoloRareVSTAR:           return QStringLiteral("Holo Rare VSTAR");
        case CardRarity::AmazingRare:             return QStringLiteral("Amazing Rare");
        case CardRarity::Radiant:                 return QStringLiteral("Radiant Rare");
        case CardRarity::RainbowRare:             return QStringLiteral("Rainbow Rare");
        case CardRarity::SecretRare:              return QStringLiteral("Secret Rare");
        case CardRarity::RareHoloStar:            return QStringLiteral("Rare Holo ★");
        case CardRarity::Promo:                   return QStringLiteral("Promo");
        case CardRarity::Shining:                 return QStringLiteral("Shining");
    }
    return QString();
}

// GUI — the human-facing name of a CardRarityGroup, used as a heading in the rarity
// picker and as a section title in its ⓘ dialog. Retired has one although it is never
// shown, so the switch can stay exhaustive.
inline QString rarityGroupLabel(CardRarityGroup group) {
    switch (group) {
        case CardRarityGroup::Core:    return QStringLiteral("Core");
        case CardRarityGroup::Modern:  return QStringLiteral("Modern");
        case CardRarityGroup::Special: return QStringLiteral("Special");
        case CardRarityGroup::Legacy:  return QStringLiteral("Legacy");
        case CardRarityGroup::Promo:   return QStringLiteral("Promo");
        case CardRarityGroup::Retired: return QStringLiteral("Retired");
    }
    return QString();
}

// GUI — a one-line note on what a whole group is, shown under its heading in the ⓘ
// dialog. Rarity is not one ladder, so the reader needs to know what each branch IS
// before the individual entries mean anything.
inline QString rarityGroupDescription(CardRarityGroup group) {
    switch (group) {
        case CardRarityGroup::Core:
            return QStringLiteral("The everyday booster-pack rarities, in every era.");
        case CardRarityGroup::Modern:
            return QStringLiteral(
                "The premium line of the Scarlet & Violet and Mega Evolution eras.");
        case CardRarityGroup::Special:
            return QStringLiteral(
                "Tied to one set or one card mechanic rather than to a general tier.");
        case CardRarityGroup::Legacy:
            return QStringLiteral(
                "Earlier eras. Several are families that existed only for a few sets.");
        case CardRarityGroup::Promo:
            return QStringLiteral(
                "How the card was distributed rather than how rare a pack made it.");
        case CardRarityGroup::Retired:
            return QString();
    }
    return QString();
}

// GUI — a one-sentence plain-language description of what a CardRarity means, for the
// "ⓘ/info" affordance next to the rarity picker (the terms read as opaque jargon
// otherwise). Where a rarity has a distinctive symbol it is named in the text. The era
// is left to the group heading rather than repeated per entry. Exhaustive switch so a
// new CardRarity fails -Wswitch under -Werror.
inline QString rarityDescription(CardRarity rarity) {
    switch (rarity) {
        case CardRarity::Common:
            return QStringLiteral(
                "Marked with a ● symbol. The most frequently pulled cards in booster "
                "packs — usually Basic Pokémon and simple Trainer cards.");
        case CardRarity::Uncommon:
            return QStringLiteral(
                "Marked with a ◆ symbol. Less common than Commons — often evolved "
                "Pokémon and stronger Trainer cards.");
        case CardRarity::Rare:
            return QStringLiteral(
                "Marked with a ★ symbol. The standard rare tier. Older printings could "
                "be holo or non-holo; from Scarlet & Violet the standard Rare in English "
                "booster packs is holofoil.");
        case CardRarity::DoubleRare:
            return QStringLiteral(
                "Marked with ★★ in black (abbreviated RR). The standard versions of "
                "Pokémon ex, including Tera Pokémon ex.");
        case CardRarity::UltraRare:
            return QStringLiteral(
                "Marked with ★★ in silver foil (abbreviated UR). Full-art Pokémon ex and "
                "full-art Supporters. The term had broader meanings in older eras, so an "
                "older card's exact sense depends on its set.");
        case CardRarity::IllustrationRare:
            return QStringLiteral(
                "Marked with one gold ★ (abbreviated IR). Full-art illustrations, "
                "normally of Pokémon that are not Pokémon ex, showing the Pokémon within "
                "a wider scene rather than as a portrait.");
        case CardRarity::SpecialIllustrationRare:
            return QStringLiteral(
                "Marked with ★★ in gold (abbreviated SIR). Premium alternate-art cards, "
                "generally of Pokémon ex and Supporters — the chase cards of a set.");
        case CardRarity::MegaHyperRare:
            return QStringLiteral(
                "The Mega Evolution series' most premium tier, for Mega Evolution Pokémon "
                "ex, marked with a distinctive gold rarity symbol and elaborate gold "
                "treatments.");
        case CardRarity::AceSpec:
            return QStringLiteral(
                "Powerful Trainer or Special Energy cards, magenta-framed in the modern "
                "era. A deck may hold only one ACE SPEC card in total, whatever its name. "
                "First seen in Black & White, returned in Temporal Forces.");
        case CardRarity::ShinyRare:
            return QStringLiteral(
                "Shiny Pokémon in sets such as Paldean Fates — usually the Shiny version "
                "of an ordinary Pokémon rather than of a Pokémon ex.");
        case CardRarity::ShinyUltraRare:
            return QStringLiteral(
                "The higher Shiny tier, used mainly for Shiny Pokémon ex.");
        case CardRarity::HyperRare:
            return QStringLiteral(
                "Marked with ★★★ in gold (abbreviated HR). The Scarlet & Violet gold "
                "cards — highly stylised gold Pokémon, Trainer, or Energy cards.");
        case CardRarity::BlackWhiteRare:
            return QStringLiteral(
                "Striking black-or-white monochrome artwork with special foil and texture "
                "treatments, introduced with Black Bolt and White Flare.");
        case CardRarity::MegaAttackRare:
            return QStringLiteral(
                "Mega Evolution Pokémon ex whose attack is written prominently across the "
                "illustration, including Japanese attack text on English cards.");
        case CardRarity::FuturisticRare:
            return QStringLiteral(
                "Evocative, futuristic visuals, debuting with the 30th Celebration set.");
        case CardRarity::RareHolo:
            return QStringLiteral(
                "A standard Rare given a holographic treatment — the distinction that "
                "mattered while ordinary Rares were still printed non-holo.");
        case CardRarity::RareHoloEX:
            return QStringLiteral(
                "Pokémon-ex of the original EX era, and the later Pokémon-EX of the "
                "Black & White and XY eras.");
        case CardRarity::RareHoloGX:
            return QStringLiteral(
                "Pokémon-GX of the Sun & Moon era — rule-box Pokémon with a GX attack, "
                "usually worth two Prize cards.");
        case CardRarity::RareHoloLvX:
            return QStringLiteral(
                "Pokémon LV.X of the Diamond & Pearl era, played on top of an existing "
                "copy of the Pokémon via the Level-Up mechanic.");
        case CardRarity::RarePrime:
            return QStringLiteral(
                "Prime Pokémon of the HeartGold & SoulSilver era — close-up artwork and a "
                "premium holo treatment, but not a separate card class.");
        case CardRarity::RareLegend:
            return QStringLiteral(
                "Two-card Pokémon of the HeartGold & SoulSilver era: an upper and a lower "
                "half, played together.");
        case CardRarity::RareBreak:
            return QStringLiteral(
                "Pokémon BREAK of the XY era — sideways gold cards placed over an "
                "existing Pokémon by BREAK Evolution.");
        case CardRarity::HoloRareV:
            return QStringLiteral(
                "The standard rarity for Pokémon V — rule-box Pokémon of the Sword & "
                "Shield era, usually worth two Prize cards.");
        case CardRarity::HoloRareVMAX:
            return QStringLiteral(
                "Standard Pokémon VMAX — Dynamax and Gigantamax Pokémon, usually worth "
                "three Prize cards.");
        case CardRarity::HoloRareVSTAR:
            return QStringLiteral(
                "Standard Pokémon VSTAR — the VSTAR Power mechanic, usually worth two "
                "Prize cards.");
        case CardRarity::AmazingRare:
            return QStringLiteral(
                "A distinctive rainbow splash spilling out beyond the artwork window, "
                "introduced in Vivid Voltage.");
        case CardRarity::Radiant:
            return QStringLiteral(
                "Radiant Pokémon — Shiny Pokémon with a distinctive foil treatment, "
                "limited to one per deck.");
        case CardRarity::RainbowRare:
            return QStringLiteral(
                "Rainbow-coloured secret cards, used heavily in the Sun & Moon and "
                "Sword & Shield eras and no longer part of the current system.");
        case CardRarity::SecretRare:
            return QStringLiteral(
                "The older umbrella term for cards numbered beyond the nominal set size "
                "(215/202, say). Scarlet & Violet replaced most of it with the more "
                "specific Illustration, Special Illustration, and Hyper Rare.");
        case CardRarity::RareHoloStar:
            return QStringLiteral(
                "Pokémon Star of the EX era, known to collectors as gold star cards: an "
                "alternate-coloured Pokémon with a ☆ after its name.");
        case CardRarity::Promo:
            return QStringLiteral(
                "Distributed through products, events, tins, or boxes instead of booster "
                "packs. Black Star Promos carry a promo symbol rather than a rarity one.");
        case CardRarity::Shining:
            return QStringLiteral(
                "No longer offered: the term covered several unrelated treatments across "
                "eras, and Pokémon now files those cards under their own rarities.");
    }
    return QString();
}

}  // namespace pokedex
