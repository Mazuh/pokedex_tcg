#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "core/domain/card_rarity.h"

namespace pokedex {

// GUI — best-effort map from a pokemontcg.io rarity string (CardCandidate::rarity) to
// our CardRarity enum, used to pre-fill the rarity picker when a card is chosen in the
// finder. The API's vocabulary is messier than ours and drifts as sets ship, so this
// only recognizes the strings that map cleanly; anything else — a blank, an unknown, or
// a variant we don't model — returns nullopt, leaving the field blank for the user to
// set by hand. It is a convenience, never authoritative.
//
// Because lookup is EXACT-match with nullopt on a miss, a key that turns out to be
// spelled differently upstream simply never fires: adding a plausible alias costs
// nothing and can't produce a wrong answer, only a missing one.
inline std::optional<CardRarity> rarityFromCatalog(const std::string& apiRarity) {
    static const std::unordered_map<std::string, CardRarity> kByApiString = {
        {"Common", CardRarity::Common},
        {"Uncommon", CardRarity::Uncommon},
        {"Rare", CardRarity::Rare},
        // Scarlet & Violet onward.
        {"Double Rare", CardRarity::DoubleRare},
        {"Ultra Rare", CardRarity::UltraRare},
        {"Rare Ultra", CardRarity::UltraRare},
        {"Illustration Rare", CardRarity::IllustrationRare},
        {"Special Illustration Rare", CardRarity::SpecialIllustrationRare},
        {"Hyper Rare", CardRarity::HyperRare},
        {"ACE SPEC Rare", CardRarity::AceSpec},
        {"Rare ACE", CardRarity::AceSpec},
        {"Shiny Rare", CardRarity::ShinyRare},
        {"Shiny Ultra Rare", CardRarity::ShinyUltraRare},
        {"Black White Rare", CardRarity::BlackWhiteRare},
        {"Promo", CardRarity::Promo},
        // Legacy strings the API still returns for older cards.
        {"Rare Holo", CardRarity::RareHolo},
        {"Rare Holo EX", CardRarity::RareHoloEX},
        {"Rare Holo GX", CardRarity::RareHoloGX},
        {"Rare Holo LV.X", CardRarity::RareHoloLvX},
        {"Rare Holo Star", CardRarity::RareHoloStar},
        {"Rare Prime", CardRarity::RarePrime},
        {"LEGEND", CardRarity::RareLegend},
        {"Rare BREAK", CardRarity::RareBreak},
        {"Rare Holo V", CardRarity::HoloRareV},
        {"Rare Holo VMAX", CardRarity::HoloRareVMAX},
        {"Rare Holo VSTAR", CardRarity::HoloRareVSTAR},
        {"Amazing Rare", CardRarity::AmazingRare},
        {"Radiant Rare", CardRarity::Radiant},
        {"Rare Rainbow", CardRarity::RainbowRare},
        {"Rare Secret", CardRarity::SecretRare},
        // Deliberately UNMAPPED: "Rare Shining" / "Shining" (CardRarity::Shining is
        // retired, so filling it in would only be blanked by the picker — returning
        // nullopt says the same thing honestly), and "Trainer Gallery Rare Holo",
        // which we do not model at all.
    };
    const auto it = kByApiString.find(apiRarity);
    return it != kByApiString.end() ? std::optional<CardRarity>(it->second) : std::nullopt;
}

}  // namespace pokedex
