#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/app/card_price_dto.h"
#include "core/domain/types.h"

namespace pokedex {

// Thrown when a payload is not valid JSON at all. A payload that IS valid JSON
// but is missing or mistypes individual fields does NOT throw — those fields
// degrade to blanks (see below), so a partial response still yields usable rows.
class CardCatalogParseError : public std::runtime_error {
    using std::runtime_error::runtime_error;
};

// APP — pure parsers for the pokemontcg.io JSON payloads. Qt-free: nlohmann/json
// is a PRIVATE implementation detail (these signatures take a JSON string and
// return plain structs), so the seam stays headlessly unit-testable against saved
// fixtures with no HTTP. Every extractor is defensive — a missing/wrong-typed
// field yields an empty string / zero rather than throwing.

// Parse the /v2/sets response into the set table. Entries with an empty id are
// skipped; a null/absent ptcgoCode becomes an empty string (25 sets have none).
std::vector<CardSetInfo> parseSetsResponse(const std::string& json);

// Parse a /v2/cards search response into candidates. `sets` is the parsed set
// table: each card's set.id is looked up there to resolve the printed expansion
// code (`ptcgoCode`) and `printedTotal` — the embedded card.set is NOT trusted
// for the code because it can omit a ptcgoCode the set actually has. The
// collector number is composed as "number/printedTotal" (or just "number" when
// printedTotal is unknown); it is kept as a string since promos are non-numeric.
std::vector<CardCandidate> parseCardSearchResponse(const std::string& json,
                                                   const std::vector<CardSetInfo>& sets);

// Parse a /v2/cards/{id} single-card response into price observations, reading the
// `tcgplayer` (USD, per-variant) and `cardmarket` (EUR, flat) blocks. Each numeric
// price becomes one CardPrice row keyed by the card's own id; a non-positive value
// (a zero/absent metric — e.g. cardmarket's germanProLow) is skipped as noise. The
// returned rows have an empty `id` (the caller mints one on persist) and provenance
// "tcgplayer"/"cardmarket". `observedAt` is the vendor block's printed `updatedAt`
// date (format "YYYY/MM/DD", taken at midnight UTC); `fallbackObservedAt` is used
// when that date is missing or malformed (the caller passes the fetch time, so the
// parser stays clock-free and deterministic). A payload with neither block yields
// no rows. Robust to `data` being an object (the single-card endpoint) or the first
// element of a `data` array (a search response).
std::vector<CardPrice> parseCardPrices(const std::string& json,
                                       Timestamp fallbackObservedAt);

// Resolve a user-typed set filter to the set ids it matches, for reliable
// set.id-based search narrowing. Matches an exact printed code (e.g. "OBF") OR a
// case-insensitive substring of the set name (e.g. "mcdonald" → every McDonald's
// Collection year — the only way to narrow to a code-less set). Returns every match
// (a code can map to two sets; a name substring to many); empty when nothing
// matches or the input is blank.
std::vector<std::string> resolveSetFilterToIds(const std::string& typed,
                                               const std::vector<CardSetInfo>& sets);

}  // namespace pokedex
