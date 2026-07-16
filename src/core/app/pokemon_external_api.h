#pragma once

#include <string>

#include "core/domain/types.h"

namespace pokedex {

// The kind of media asset to fetch for a subject. Extensible: today only
// OfficialArtwork is wired, but sprites / other imagery slot in here without
// touching the service or the views that consume it.
enum class MediaKind {
    OfficialArtwork,
};

// The subject of an external-API request: a Pokémon species. dexNumber drives
// the artwork URL and the override-table lookup; name feeds the default
// resourceName transform when no override applies.
struct MediaSubject {
    PokemonDexNum dexNumber;
    std::string name;
};

// A resolved fetch instruction: where to GET the asset, and the external API's
// canonical resource identity for the subject. resourceName is what the GUI's
// cache layout keys on (e.g. "nidoran-m", "mime-jr", "pikachu") so cached files
// are human-readable and independent of the source URL scheme. An empty url
// means this API cannot serve the requested (subject, kind).
struct MediaRequest {
    std::string url;
    std::string resourceName;
};

// APP — the swappable external-API seam. The GUI depends only on this interface;
// it never names PokeAPI, a URL, or a filename, so replacing the source (should
// the free API disappear) is a one-line change at the composition root. It is
// conceived as the adapter for Pokémon media and info: only media (official
// artwork) is wired today, but a resolveInfo(...) method can be added later
// without disturbing callers. Kept Qt-free and pure so it is headlessly
// unit-testable — the actual HTTP transport is done GUI-side using what this
// resolves.
class PokemonExternalApi {
public:
    virtual ~PokemonExternalApi() = default;

    virtual MediaRequest resolveMedia(const MediaSubject& subject, MediaKind kind) const = 0;
};

}  // namespace pokedex
