#pragma once

#include <string>

#include "core/app/pokemon_external_api.h"

namespace pokedex {

// APP — the default PokemonExternalApi implementation, backed by PokeAPI's
// public data. Official artwork is served as static PNGs on GitHub raw, keyed by
// National Dex number; info endpoints (a future concern) key on the species'
// resource name. This is the one place that knows those PokeAPI conventions.
class PokeApi : public PokemonExternalApi {
public:
    MediaRequest resolveMedia(const MediaSubject& subject, MediaKind kind) const override;

private:
    // The API's canonical resource id for a species: a default transform of the
    // display name (lowercase, fold diacritics, drop punctuation, spaces→'-')
    // covers the vast majority, with a small override for the cases the transform
    // cannot recover. See the .cpp for the derivation rules and the override table.
    std::string resourceNameFor(const MediaSubject& subject) const;
};

}  // namespace pokedex
