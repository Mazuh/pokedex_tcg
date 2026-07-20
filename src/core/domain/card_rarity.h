#pragma once

namespace pokedex {

// COLLECTION — a physical CardCopy's rarity: how hard the card is to obtain,
// determined by its rarity classification and symbol (NOT by whether it is
// holographic — that is the independent CardFoil / finish). Optional on a
// CardCopy: a copy may be recorded without one (nullopt).
//
// The first block is the modern rarity scale; the LEGACY block below it collects
// the rarities used in older Pokémon TCG eras. The two groups are declared in
// this order (modern, then legacy) and the order is pinned by a test — the GUI's
// rarity picker and its info popover both rely on it to keep the legacy rarities
// grouped at the end. These are categorical, not a best-to-worst ranking.
enum class CardRarity {
    // Modern scale.
    Common,                   // ●
    Uncommon,                 // ◆
    Rare,                     // ★
    DoubleRare,               // ★★ (Pokémon ex and equivalents) — "RR"
    IllustrationRare,         // full-art illustration focus — "IR"
    UltraRare,                // Full Art Pokémon/Supporter/Special Energy — "UR"
    SpecialIllustrationRare,  // premium full-art chase card — "SIR"
    HyperRare,                // gold, textured foil — "HR"
    Promo,                    // distributed outside booster packs

    // Legacy rarities — older eras, kept selectable for older cards.
    RareHolo,     // standard holographic rare
    RareHoloEX,   // Pokémon-EX from the EX era
    RarePrime,    // Prime Pokémon (HeartGold & SoulSilver)
    RareLegend,   // two-card LEGEND Pokémon
    AmazingRare,  // rainbow burst background, unique typing
    Shining,      // special shiny Pokémon from selected expansions
    Radiant,      // shiny Pokémon with gameplay restrictions
    AceSpec,      // powerful Trainer, one copy per deck
};

}  // namespace pokedex
