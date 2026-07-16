#include "core/app/pokemon_tcg_io_api.h"

#include <cctype>
#include <string>

namespace pokedex {

namespace {

constexpr const char* kBase = "https://api.pokemontcg.io/v2";

// The most cards a single species has runs into the low hundreds; 250 is the
// API's per-page maximum, so one page covers the vast majority in a single GET
// and the printings list then paginates that vector client-side.
constexpr const char* kPageSize = "250";

// Percent-encode everything outside the RFC 3986 unreserved set, so a Lucene
// query with spaces, colons and parentheses rides safely in the URL query.
std::string urlEncode(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (const unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

}  // namespace

HttpRequest PokemonTcgIoApi::resolveSearch(const CardSearchQuery& query) const {
    // Species clause, then an optional set-narrowing clause ORing set.id values
    // (set.id, not the printed ptcgoCode, because the ptcgoCode search index is
    // unreliable). e.g. "nationalPokedexNumbers:6 (set.id:sv3 OR set.id:sv4)".
    std::string q = "nationalPokedexNumbers:" + std::to_string(query.dexNumber);
    if (!query.setIds.empty()) {
        q += " (";
        for (std::size_t i = 0; i < query.setIds.size(); ++i) {
            if (i != 0) {
                q += " OR ";
            }
            q += "set.id:" + query.setIds[i];
        }
        q += ")";
    }

    HttpRequest request;
    request.url = std::string(kBase) + "/cards?q=" + urlEncode(q) +
                  "&pageSize=" + kPageSize;
    return request;
}

HttpRequest PokemonTcgIoApi::resolveSets() const {
    HttpRequest request;
    request.url = std::string(kBase) + "/sets?pageSize=" + kPageSize;
    return request;
}

}  // namespace pokedex
