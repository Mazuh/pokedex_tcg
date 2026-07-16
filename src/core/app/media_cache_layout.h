#pragma once

#include <string>

#include "core/app/pokemon_external_api.h"

namespace pokedex {

// APP — the on-disk cache layout, computed independently of any external API so
// it stays stable across an API swap. Given the API-resolved resourceName and
// the asset kind, it returns the media-cache-relative path a media file is
// stored under, e.g. mediaCacheRelPath("mime-jr", OfficialArtwork) ==
// "pokemon/mime-jr/official-artwork.png". The GUI service joins this onto the
// workspace media dir. Human-readable and organized by subject + kind rather
// than by the source's URL scheme.
std::string mediaCacheRelPath(const std::string& resourceName, MediaKind kind);

}  // namespace pokedex
