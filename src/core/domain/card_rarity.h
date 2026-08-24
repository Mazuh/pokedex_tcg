#pragma once

namespace pokedex {

// COLLECTION — a physical CardCopy's rarity: its printed rarity classification and
// symbol (NOT whether it is holographic — that is the independent CardFoil / finish).
// Optional on a CardCopy: a copy may be recorded without one (nullopt).
//
// This is NOT one ladder from Common to the chase card. Modern Pokémon rarity is a set
// of era-specific BRANCHES that reuse each other's symbols: Scarlet & Violet's Ultra
// Rare means something different from the pre-SV umbrella term, ★★ marks both a Double
// Rare (black stars) and an Ultra Rare (silver), and whole families (V/VMAX/VSTAR, GX,
// LV.X) exist only inside one era. So the values are grouped by ERA/FAMILY — see
// CardRarityGroup — and read as categorical, never as a best-to-worst ranking.
//
// Declaration order is load-bearing three ways: it is kAllRarities' order, the picker's
// order, and the Rarity column's sort rank (which ranks by the underlying int). Groups
// are declared CONTIGUOUSLY and Retired last, both pinned by a test.
//
// STORAGE CONTRACT — the on-disk token is the enumerator's spelling (codecs.cpp), and an
// unknown token throws, failing a whole card-list load. So this enum is APPEND-ONLY: a
// value may be reordered or relabelled, never renamed or removed. Withdrawing an option
// from the picker is what CardRarityGroup::Retired expresses.
enum class CardRarity {
    // Core — the everyday booster-pack rarities.
    Common,    // ● circle
    Uncommon,  // ◆ diamond
    Rare,      // ★ star; holo or not depending on the era

    // Modern — the Scarlet & Violet and Mega Evolution premium line.
    DoubleRare,               // ★★ black — standard Pokémon ex — "RR"
    UltraRare,                // ★★ silver — full-art ex and Supporters — "UR"
    IllustrationRare,         // ★ gold — full-art scene art, non-ex — "IR"
    SpecialIllustrationRare,  // ★★ gold — premium alternate art — "SIR"
    MegaHyperRare,            // Mega Evolution era's elaborate gold treatment

    // Special — set-specific or mechanic-specific classifications.
    AceSpec,          // one ACE SPEC card per deck, magenta frame
    ShinyRare,        // Shiny ordinary Pokémon (e.g. Paldean Fates)
    ShinyUltraRare,   // Shiny Pokémon ex
    HyperRare,        // ★★★ gold — the Scarlet & Violet gold cards — "HR"
    BlackWhiteRare,   // monochrome art, Black Bolt / White Flare
    MegaAttackRare,   // Mega ex with the attack written across the art
    FuturisticRare,   // 30th Celebration onward

    // Legacy — rarities of earlier eras, kept selectable for older cards.
    RareHolo,       // standard holographic rare
    RareHoloEX,     // Pokémon-ex / Pokémon-EX (EX, BW, XY eras)
    RareHoloGX,     // Pokémon-GX (Sun & Moon)
    RareHoloLvX,    // Pokémon LV.X (Diamond & Pearl)
    RarePrime,      // Prime Pokémon (HeartGold & SoulSilver)
    RareLegend,     // two-card LEGEND Pokémon
    RareBreak,      // sideways gold BREAK cards (XY)
    HoloRareV,      // Pokémon V (Sword & Shield)
    HoloRareVMAX,   // Pokémon VMAX
    HoloRareVSTAR,  // Pokémon VSTAR
    AmazingRare,    // rainbow splash beyond the art window (Vivid Voltage on)
    Radiant,        // Radiant Pokémon, one per deck (Sword & Shield)
    RainbowRare,    // rainbow secret cards (Sun & Moon / Sword & Shield)
    SecretRare,     // older umbrella term for over-the-set-size cards
    RareHoloStar,   // ☆ Pokémon Star / "gold star" (EX era)

    // Distribution rather than pack rarity, but Pokémon exposes it as a rarity.
    Promo,

    // Retired — never offered in the picker, kept so copies already recorded with
    // them still load and display. Never delete one; see the storage contract above.
    Shining,  // ambiguous across eras (Neo vs. Shining Legends); superseded
};

// Which era/family a CardRarity belongs to. Drives the picker's group headings and the
// info dialog's sections; Retired means "still storable, no longer offered".
enum class CardRarityGroup { Core, Modern, Special, Legacy, Promo, Retired };

// Every CardRarity, in declaration order — the ONE list the codecs, the picker and the
// tests read, so none of them can drift from the enum (the precedent is kRegions).
//
// INVARIANT, pinned by a test: kAllRarities[i] == static_cast<CardRarity>(i).
inline constexpr CardRarity kAllRarities[] = {
    CardRarity::Common,
    CardRarity::Uncommon,
    CardRarity::Rare,
    CardRarity::DoubleRare,
    CardRarity::UltraRare,
    CardRarity::IllustrationRare,
    CardRarity::SpecialIllustrationRare,
    CardRarity::MegaHyperRare,
    CardRarity::AceSpec,
    CardRarity::ShinyRare,
    CardRarity::ShinyUltraRare,
    CardRarity::HyperRare,
    CardRarity::BlackWhiteRare,
    CardRarity::MegaAttackRare,
    CardRarity::FuturisticRare,
    CardRarity::RareHolo,
    CardRarity::RareHoloEX,
    CardRarity::RareHoloGX,
    CardRarity::RareHoloLvX,
    CardRarity::RarePrime,
    CardRarity::RareLegend,
    CardRarity::RareBreak,
    CardRarity::HoloRareV,
    CardRarity::HoloRareVMAX,
    CardRarity::HoloRareVSTAR,
    CardRarity::AmazingRare,
    CardRarity::Radiant,
    CardRarity::RainbowRare,
    CardRarity::SecretRare,
    CardRarity::RareHoloStar,
    CardRarity::Promo,
    CardRarity::Shining,
};

// The group a rarity belongs to. An exhaustive switch, so a NEW enumerator fails
// -Wswitch under -Werror until it is placed — which is what stops one being added to
// the enum and then silently missing from the picker.
constexpr CardRarityGroup rarityGroup(CardRarity rarity) {
    switch (rarity) {
        case CardRarity::Common:
        case CardRarity::Uncommon:
        case CardRarity::Rare:
            return CardRarityGroup::Core;

        case CardRarity::DoubleRare:
        case CardRarity::UltraRare:
        case CardRarity::IllustrationRare:
        case CardRarity::SpecialIllustrationRare:
        case CardRarity::MegaHyperRare:
            return CardRarityGroup::Modern;

        case CardRarity::AceSpec:
        case CardRarity::ShinyRare:
        case CardRarity::ShinyUltraRare:
        case CardRarity::HyperRare:
        case CardRarity::BlackWhiteRare:
        case CardRarity::MegaAttackRare:
        case CardRarity::FuturisticRare:
            return CardRarityGroup::Special;

        case CardRarity::RareHolo:
        case CardRarity::RareHoloEX:
        case CardRarity::RareHoloGX:
        case CardRarity::RareHoloLvX:
        case CardRarity::RarePrime:
        case CardRarity::RareLegend:
        case CardRarity::RareBreak:
        case CardRarity::HoloRareV:
        case CardRarity::HoloRareVMAX:
        case CardRarity::HoloRareVSTAR:
        case CardRarity::AmazingRare:
        case CardRarity::Radiant:
        case CardRarity::RainbowRare:
        case CardRarity::SecretRare:
        case CardRarity::RareHoloStar:
            return CardRarityGroup::Legacy;

        case CardRarity::Promo:
            return CardRarityGroup::Promo;

        case CardRarity::Shining:
            return CardRarityGroup::Retired;
    }
    return CardRarityGroup::Retired;  // unreachable for a valid enum value
}

}  // namespace pokedex
