#pragma once

#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/card_foil.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_rarity.h"
#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

// STORAGE — the encoding between domain values and their on-disk TEXT form.
// Enums persist as stable tokens (never their numeric value, which would break
// if the enum is reordered) and timestamps as ISO-8601 UTC. These live in the
// storage layer, not the domain, so the domain stays ignorant of persistence;
// repositories share them (the timestamp codec is reused by every root).

// Region <-> its stable storage token ("Kanto" … "Paldea"). regionFromText
// throws StorageError on an unrecognized token.
std::string regionToText(Region region);
Region regionFromText(const std::string& text);

// CardOwnership <-> its stable storage token ("Incoming" | "Owned" | "Removed").
// ownershipFromText throws StorageError on an unrecognized token.
std::string ownershipToText(CardOwnership ownership);
CardOwnership ownershipFromText(const std::string& text);

// CardCondition <-> its stable storage token ("NearMint" … "Damaged"). Condition
// is optional (a copy may be recorded ungraded): nullopt <-> the empty string.
// conditionFromText throws StorageError on a non-empty unrecognized token.
std::string conditionToText(std::optional<CardCondition> condition);
std::optional<CardCondition> conditionFromText(const std::string& text);

// CardRarity <-> its stable storage token ("Common" … "AceSpec"). Rarity is
// optional (a copy may be recorded without one): nullopt <-> the empty string.
// rarityFromText throws StorageError on a non-empty unrecognized token.
std::string rarityToText(std::optional<CardRarity> rarity);
std::optional<CardRarity> rarityFromText(const std::string& text);

// CardFoil <-> its stable storage token ("NonHolo" … "Textured"). Foil treatment
// is optional (a copy may be recorded without one): nullopt <-> the empty string.
// foilFromText throws StorageError on a non-empty unrecognized token.
std::string foilToText(std::optional<CardFoil> foil);
std::optional<CardFoil> foilFromText(const std::string& text);

// Timestamp <-> ISO-8601 UTC, second precision ("YYYY-MM-DDThh:mm:ssZ").
// Sub-second parts are truncated on encode. timestampFromIso throws StorageError
// when the text is not in that exact form.
std::string timestampToIso(Timestamp when);
Timestamp timestampFromIso(const std::string& text);

}  // namespace pokedex
