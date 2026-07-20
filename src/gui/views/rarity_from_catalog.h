#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "core/domain/card_rarity.h"

namespace pokedex {

// GUI — best-effort map from a pokemontcg.io rarity string (CardCandidate::rarity)
// to our CardRarity enum, used to pre-fill the rarity picker when a card is chosen
// in the finder. The API's vocabulary is larger and messier than our fixed enum
// (e.g. "Rare Holo GX", "Trainer Gallery Rare Holo", "Rare Secret"), so this only
// recognizes the strings that map cleanly; anything else — a blank, an unknown, or a
// variant we don't model — returns nullopt, leaving the field blank for the user to
// set by hand. It is a convenience, never authoritative: the picker stays editable.
inline std::optional<CardRarity> rarityFromCatalog(const std::string& apiRarity) {
    static const std::unordered_map<std::string, CardRarity> kByApiString = {
        {"Common", CardRarity::Common},
        {"Uncommon", CardRarity::Uncommon},
        {"Rare", CardRarity::Rare},
        {"Double Rare", CardRarity::DoubleRare},
        {"Illustration Rare", CardRarity::IllustrationRare},
        {"Ultra Rare", CardRarity::UltraRare},
        {"Special Illustration Rare", CardRarity::SpecialIllustrationRare},
        {"Hyper Rare", CardRarity::HyperRare},
        {"Promo", CardRarity::Promo},
        // Legacy strings the API still returns for older cards.
        {"Rare Holo", CardRarity::RareHolo},
        {"Rare Holo EX", CardRarity::RareHoloEX},
        {"Rare Prime", CardRarity::RarePrime},
        {"LEGEND", CardRarity::RareLegend},
        {"Amazing Rare", CardRarity::AmazingRare},
        {"Rare Shining", CardRarity::Shining},
        {"Shining", CardRarity::Shining},
        {"Radiant Rare", CardRarity::Radiant},
        {"ACE SPEC Rare", CardRarity::AceSpec},
    };
    const auto it = kByApiString.find(apiRarity);
    return it != kByApiString.end() ? std::optional<CardRarity>(it->second) : std::nullopt;
}

}  // namespace pokedex
