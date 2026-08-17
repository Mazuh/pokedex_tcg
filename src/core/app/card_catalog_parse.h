#pragma once

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/app/card_catalog_dto.h"
#include "core/app/card_price_dto.h"
#include "core/domain/card_reference.h"
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

// The outcome of parsing a per-card price payload. `cardPresent` says whether the response
// actually carried a resolvable card object (vs a degraded/error body with no card node);
// `prices` are its extracted rows. The distinction lets a caller tell a genuinely price-less
// card (card present but no vendor blocks — a delisted card, or a set the provider hasn't
// priced yet) apart from a degraded response (no card object at all): the former should
// overwrite the cache (caching the blank), the latter should preserve any good cached prices.
struct CardPricesParse {
    bool cardPresent = false;
    std::vector<CardPrice> prices;
};

// Parse a tcgdex /v2/en/cards/{id} single-card response into price observations. tcgdex is
// the app's sole automated pricing PROVIDER (a free aggregator, not a source of truth):
// unlike pokemontcg.io it covers brand-new sets and is addressable by set+collector-number,
// so a card the metadata catalog hasn't ingested can still be priced. Its payload differs
// from pokemontcg.io's: prices live under `variants_detailed[].pricing.{cardmarket,
// tcgplayer}`, and — crucially — tcgdex's metric NAMES ("trend"/"avg"/"marketPrice"…) are
// normalized here to a canonical vocabulary ("trendPrice"/"averageSellPrice"/"market"… —
// pokemontcg.io's names, still emitted for the finder hint), so the cache and every display
// stay source-agnostic (the same CardPrice rows, the same vendorBest pick). Only known price
// metrics are read — the vendor blocks also carry non-price numbers (`idProduct`,
// `productId`) a blind scan would misread as money. cardmarket is EUR and flat; tcgplayer is
// USD and nested by finish. Standard-size variants are preferred (an oversized jumbo/
// lenticular printing is a different product); a card with only oversized printings still
// yields their prices. Dates are ISO-8601 (`updated`); a missing/malformed one uses
// `fallbackObservedAt`. Non-positive / sub-cent metrics are dropped as noise. Rows carry an
// empty id (minted on persist) and the payload's own card id. Throws CardCatalogParseError on
// non-JSON. `...Result` additionally reports `cardPresent` (an error/404 body has no card).
std::vector<CardPrice> parseTcgdexCardPrices(const std::string& json, Timestamp fallbackObservedAt);
CardPricesParse parseTcgdexCardPricesResult(const std::string& json, Timestamp fallbackObservedAt);

// Parse a tcgdex /v2/en/sets response — a FLAT JSON array of {id, name, cardCount}, not the
// pokemontcg.io "data"-wrapped shape — into the set table that maps a copy's printed set to
// a tcgdex set id (see resolveTcgdexCardId). Entries with an empty id are skipped; ptcgoCode
// is left blank (tcgdex publishes none) and printedTotal carries cardCount.total. A
// non-array payload yields no sets rather than throwing.
std::vector<CardSetInfo> parseTcgdexSets(const std::string& json);

// Resolve a copy's printed identity to a tcgdex card id ("mep-013" == setId "-" localId) for
// the price lookup, using the tcgdex set table (parseTcgdexSets) to map the set to a tcgdex
// set id. Because tcgdex is addressable by set+number this needs no catalog SEARCH — pricing
// is derived directly from what the copy already records. Only EXACT set matches are trusted
// (a wrong link would show another card's prices): the set name equal to a tcgdex set NAME
// (the main-set path — the tcgdex id "sv03" is nothing like the printed "OBF", but the names
// match), tried first, then the expansion code AS a tcgdex set id (promos whose printed code
// is the id, e.g. "MEP"→"mep"). The localId is the collector number's leading printing part
// ("125/197"→"125", "013"→"013"). Returns nullopt when no collector number is recorded or the
// set can't be exactly identified — the caller then shows a "can't price this yet" hint
// rather than guessing a wrong card (no fuzzy substring matching, no existence check).
std::optional<std::string> resolveTcgdexCardId(const CardReference& ref,
                                               const std::vector<CardSetInfo>& tcgdexSets);

// Resolve a set filter to the set ids it matches. The finder no longer TYPES a filter —
// a search is narrowed by an exact set id picked from the dropdown — so this is now the
// resolver for a filter that arrives from somewhere else: a card scanner's reading, or
// the set of the last card added. Its callers act only on an unambiguous single match,
// which is why returning every match still matters. The filter is read WORD BY WORD (whitespace-
// separated, ignoring any word with no alphanumeric character): a set matches when
// EVERY word lands on it, each either as its exact printed code (e.g. "OBF") or as a
// case-insensitive substring of its name (e.g. "mcdonald" → every McDonald's
// Collection year — the only way to narrow to a code-less set). Name matching needs
// the whole filter to be 3+ chars; a 1-2 char fragment would match a large fraction
// of the ~150 sets. Returns every match (a code can map to two sets; a name
// substring to many); empty when nothing matches or the input is blank.
//
// Matching per word rather than as one substring is deliberately FORGIVING, and is
// strictly more permissive than a whole-string substring (if the filter is a
// substring of the name, so is each of its words). It buys two things: a "CODE — Name"
// label resolves verbatim (the decoration is in no set's name, so as one substring it
// matched nothing at all), and words may be given in any order or with the code first
// ("cri chaos rising"). Note it reads a trailing number as part of the SET ("POP 9"
// pins POP Series 9), so a filter carrying a collector number ("OBF 125") matches
// nothing — the finder narrows by set alone, and a caller passing a scanner's reading
// should hand over the set part only.
std::vector<std::string> resolveSetFilterToIds(const std::string& typed,
                                               const std::vector<CardSetInfo>& sets);

}  // namespace pokedex
