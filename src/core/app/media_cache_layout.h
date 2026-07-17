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

// APP — the media-cache-relative path an owned copy's card image is stored under,
// keyed by the copy's synthetic id, e.g. cardImageCacheRelPath("a1b2") ==
// "cards/a1b2.png". Unlike Pokémon artwork it is not sourced from any external API
// (it is the one image a committed copy keeps, saved at creation), so the copy id
// — filesystem-safe and available both when saving and when displaying — is the
// key. The stable name also lets a user later drop in their own photo by hand.
std::string cardImageCacheRelPath(const std::string& copyId);

}  // namespace pokedex
