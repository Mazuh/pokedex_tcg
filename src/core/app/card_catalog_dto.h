#pragma once

#include <string>

#include "core/domain/card_reference.h"

namespace pokedex {

// APP — one set's identity from the /v2/sets table, a recomputed projection of
// external data (never stored), so it lives in app/ not domain/. `id` is the
// API's stable set identifier used for reliable search narrowing; `ptcgoCode` is
// the printed expansion code shown on the card (may be empty — 25 sets lack one,
// and 8 codes are shared by two sets, so a code is not a unique key);
// `printedTotal` is the denominator of a card's collector number ("151/165").
struct CardSetInfo {
    std::string id;
    std::string ptcgoCode;
    std::string name;
    int printedTotal = 0;
};

// APP — one card printing surfaced by a search: the autofill payload plus display
// metadata. A recomputed projection → app/, not domain/. `cardRef` maps onto the
// domain CardReference, but its `language` is deliberately left empty: the card
// source is English-only and has no language field, so the user always picks that
// separately (the selector never sets a language). The image URLs are
// display-only — in this task they are shown from memory and never cached to disk.
struct CardCandidate {
    std::string id;  // the API's stable card id (e.g. "sv3-125"); a per-row key
    CardReference cardRef;
    std::string name;
    std::string imageUrlSmall;
    std::string imageUrlLarge;
    std::string rarity;
    std::string artist;
    std::string setName;
    std::string setId;
};

}  // namespace pokedex
