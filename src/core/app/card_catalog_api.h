#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/domain/types.h"

namespace pokedex {

// A resolved GET target for the external card catalog. Parallels MediaRequest,
// but a card search returns JSON (parsed in card_catalog_parse) rather than an
// image, so it carries only the url.
struct HttpRequest {
    std::string url;
};

// The input to a card search, optionally narrowed to specific sets. A search is
// scoped EITHER by species (dexNumber set) OR by card name (nameQuery non-empty)
// — the two ways to find a printing. Most cards depict a species, so dexNumber is
// the common path; nameQuery is how a species-free card (a Trainer or Energy
// card, which has no national dex number) is found. When both are set dexNumber
// wins; when neither is, the search is unscoped. setIds are the API's stable set
// identifiers (e.g. "sv3"), NOT printed expansion codes — narrowing by set.id is
// reliable where the ptcgoCode search index is not (see card_catalog_parse / the
// CardCatalogApi docstring). An empty setIds means "no set narrowing".
struct CardSearchQuery {
    std::optional<PokemonDexNum> dexNumber;
    std::string nameQuery;
    std::vector<std::string> setIds;
};

// APP — the swappable seam for the external CARD catalog, deliberately separate
// from the Pokémon media seam (PokemonExternalApi): searching cards is a distinct
// concern from fetching a species' artwork. Like that seam it is Qt-free and
// pure — it only *resolves* where to GET; the HTTP transport happens GUI-side and
// the JSON parsing happens in card_catalog_parse. The GUI depends only on this
// interface, so the card source can be swapped at the composition root without
// touching callers. Kept headlessly unit-testable: the resolvers are pure string
// transforms with no I/O.
class CardCatalogApi {
public:
    virtual ~CardCatalogApi() = default;

    // Where to GET the printings of a species (optionally narrowed to setIds).
    virtual HttpRequest resolveSearch(const CardSearchQuery& query) const = 0;

    // Where to GET the full set list — the code/id/printedTotal table that
    // card_catalog_parse turns into the in-memory set lookup. Resolved once and
    // cached by the caller for the app's lifetime.
    virtual HttpRequest resolveSets() const = 0;
};

}  // namespace pokedex
