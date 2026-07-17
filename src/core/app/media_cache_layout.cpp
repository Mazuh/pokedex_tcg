#include "core/app/media_cache_layout.h"

namespace pokedex {

namespace {

// The filename stem for each media kind — the human-readable part of the cache
// path (e.g. "official-artwork" → official-artwork.png).
std::string kindToken(MediaKind kind) {
    switch (kind) {
        case MediaKind::OfficialArtwork:
            return "official-artwork";
    }
    return {};  // unreachable; keeps -Werror happy without a default case
}

}  // namespace

std::string mediaCacheRelPath(const std::string& resourceName, MediaKind kind) {
    return "pokemon/" + resourceName + "/" + kindToken(kind) + ".png";
}

std::string cardImageCacheRelPath(const std::string& copyId) {
    return "cards/" + copyId + ".png";
}

}  // namespace pokedex
