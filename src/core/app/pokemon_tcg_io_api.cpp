#include "core/app/pokemon_tcg_io_api.h"

#include <cctype>
#include <sstream>
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

// Backslash-escape the Lucene query-syntax metacharacters, so a user-typed word is
// matched literally rather than parsed as an operator. Without this a name with a
// paren / quote / colon (e.g. "Research (Magnolia)") produces a malformed query the
// API rejects with 400. The wildcard `*` we add ourselves is applied AFTER escaping,
// so it stays a wildcard; a literal `*`/`?` inside the user's text is escaped here.
std::string escapeLucene(const std::string& word) {
    static const std::string kSpecial = R"(+-&|!(){}[]^"~*?:\/)";
    std::string out;
    out.reserve(word.size());
    for (const char c : word) {
        if (kSpecial.find(c) != std::string::npos) {
            out += '\\';
        }
        out += c;
    }
    return out;
}

// Build the Lucene name clause for a by-name search. Each whitespace-separated word
// becomes a `name:*word*` substring term, ANDed together (Lucene's default). Per-word
// wildcards are what actually work on pokemontcg.io: a single quoted phrase with a
// trailing wildcard ("boss orders*") matches nothing, whereas `name:*boss* name:*orders*`
// finds "Boss's Orders" regardless of the apostrophe or word order.
std::string nameClause(const std::string& query) {
    std::istringstream words(query);
    std::string word;
    std::string clause;
    while (words >> word) {
        if (!clause.empty()) {
            clause += " ";
        }
        clause += "name:*" + escapeLucene(word) + "*";
    }
    return clause;
}

}  // namespace

HttpRequest PokemonTcgIoApi::resolveSearch(const CardSearchQuery& query) const {
    // The scope clause — by species when a dex number is given, else by card name
    // (per-word substring terms, so "boss orders" finds "Boss's Orders"). Then an
    // optional set-narrowing clause ORing set.id values (set.id, not the printed
    // ptcgoCode, because the ptcgoCode search index is unreliable). e.g.
    // "nationalPokedexNumbers:6 (set.id:sv3 OR set.id:sv4)" or
    // "name:*boss* name:*orders* (set.id:sv3)".
    std::string q;
    if (query.dexNumber) {
        q = "nationalPokedexNumbers:" + std::to_string(*query.dexNumber);
    } else {
        q = nameClause(query.nameQuery);
    }
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

HttpRequest PokemonTcgIoApi::resolveCardById(const std::string& cardId) const {
    // The card id is a path segment, so it is URL-encoded (real ids are unreserved
    // ASCII and pass through unchanged, but encoding keeps a malformed id safe).
    HttpRequest request;
    request.url = std::string(kBase) + "/cards/" + urlEncode(cardId);
    return request;
}

HttpRequest PokemonTcgIoApi::resolveSets() const {
    HttpRequest request;
    request.url = std::string(kBase) + "/sets?pageSize=" + kPageSize;
    return request;
}

}  // namespace pokedex
