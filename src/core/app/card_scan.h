#pragma once

#include <optional>
#include <string>

#include "core/app/ai_assistant.h"
#include "core/domain/types.h"

namespace pokedex {

// APP — the result of asking the assistant to read a photographed card. The
// assistant's ONE job is to read the printing off the card and hand back a plain
// SEARCH STRING (plus the components it read), never to "find" the card: the app's
// own deterministic search (the flexible My Cards filter, the catalog finder) does
// the matching. So this is a small, provider-neutral projection of the model's JSON
// reply, not a catalog lookup.
//
// `query` is the ready-to-use search string — a distinctive part of the English set
// name (or the set code, when clearly printed) followed by the collector number,
// e.g. "Base Set 4/102" or "collection 2021 1/25". It is what the My Cards search
// box and the add-page finder are pre-filled with. The individual components are
// carried too, so the add form can pre-fill its printed-identity fields.
//
// When the model can't read a card (nothing recognizable, too blurry, not a Pokémon
// card) `identified` is false and `note` explains why — the caller shows that and
// lets the user retry or type by hand, rather than searching on a bad guess.
struct ScannedCard {
    bool identified = false;
    std::string cardName;          // the printed card name, e.g. "Bulbasaur"
    std::string setName;           // the English set name, e.g. "McDonald's Collection 2021"
    std::string setCode;           // the printed set code when present, else ""
    std::string collectorNumber;   // e.g. "1/25", "4/102"
    std::string query;             // the ready-to-use search string (see above)
    std::string note;              // set when !identified: a short human reason
};

// The system instruction that turns the assistant into a card reader: it defines the
// exact JSON shape and the query-string rule. Public so tests can pin it and the GUI
// can reuse it; pure data, no I/O.
std::string cardScanSystemInstruction();

// Build the vision AiPrompt for a card photo: the base64-encoded image plus the
// card-scan system instruction and the JSON-only hint. `base64Jpeg` is the image
// bytes already base64-encoded by the caller (the GUI encodes the webcam frame);
// `mimeType` defaults to JPEG. Pure — no clock, no I/O.
AiPrompt buildCardScanPrompt(std::string base64Jpeg, std::string mimeType = "image/jpeg");

// Parse the assistant's reply into a ScannedCard. Tolerant by design: it strips any
// ``` fences or surrounding prose, isolates the JSON object, and never throws — an
// unreadable or empty reply degrades to identified=false with a note. When the model
// reports a card but omits the query, one is synthesized from the components so the
// caller always gets something searchable.
ScannedCard parseScannedCard(const std::string& assistantText);

// A best-guess dex number of the species a scanned card name depicts, matched against the
// National Pokédex catalog. Matching is by normalized whole *word* (token), not raw
// substring: each name is split on whitespace/punctuation and lowercased ASCII-wise (so
// non-alphanumerics — apostrophes, periods, the ♀/♂ symbols — are ignored), and a species
// matches when its token sequence appears contiguously among the card name's tokens. The
// longest (most tokens, then most characters) match wins. So "Mewtwo" resolves to Mewtwo
// (not Mew), "Ash's Pikachu" and "Charizard ex" to their species, "Farfetch'd"/"Mr Mime"/
// "Nidoran" match despite punctuation, and "Parasol Lady" matches nothing (no bare "Paras"
// false positive). Returns nullopt for a name that depicts no known species (a Trainer/
// Energy card, or an unrecognized name). A first impression, not an authority — the add
// flow's finder still confirms the actual printing. Pure, catalog-only; unit-tested.
std::optional<PokemonDexNum> detectScannedSpecies(const std::string& cardName);

}  // namespace pokedex
