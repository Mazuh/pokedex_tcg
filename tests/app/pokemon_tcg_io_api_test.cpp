#include "core/app/pokemon_tcg_io_api.h"

#include <gtest/gtest.h>

#include "core/app/card_catalog_api.h"

namespace {

using pokedex::PokemonTcgIoApi;

// A plain species search: the national-dex clause, URL-encoded (the colon
// becomes %3A), at the API's max page size.
TEST(PokemonTcgIoApiTest, ResolvesSpeciesSearchByDexNumber) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({.dexNumber = 6});
    EXPECT_EQ(request.url,
              "https://api.pokemontcg.io/v2/cards"
              "?q=nationalPokedexNumbers%3A6&pageSize=250");
}

// A by-name search (a species-free card, e.g. a Trainer/Energy card) turns each word
// into a `name:*word*` substring term, ANDed — the form that actually matches on
// pokemontcg.io (a quoted phrase with a trailing wildcard matches nothing). The
// asterisks, colons and the separating space are percent-encoded.
TEST(PokemonTcgIoApiTest, ResolvesSearchByCardName) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({.nameQuery = "boss orders"});
    EXPECT_EQ(request.url,
              "https://api.pokemontcg.io/v2/cards"
              "?q=name%3A%2Aboss%2A%20name%3A%2Aorders%2A&pageSize=250");
}

// Lucene metacharacters in a typed name are backslash-escaped so they're matched
// literally, not parsed as operators (an unescaped "(" makes the API 400). The
// escaping backslash (%5C) and the term wildcards (%2A) survive URL-encoding; the
// hyphen is unreserved and stays literal.
TEST(PokemonTcgIoApiTest, EscapesLuceneMetacharactersInACardName) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({.nameQuery = "ho-oh (v)"});
    EXPECT_EQ(request.url,
              "https://api.pokemontcg.io/v2/cards"
              "?q=name%3A%2Aho%5C-oh%2A%20name%3A%2A%5C%28v%5C%29%2A&pageSize=250");
}

// Narrowing to one set appends a parenthesized set.id clause (set.id, never the
// unreliable ptcgoCode). Spaces/colons/parens are percent-encoded; the dot in
// "set.id" is unreserved and survives.
TEST(PokemonTcgIoApiTest, ResolvesSearchNarrowedToOneSet) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({.dexNumber = 6, .setIds = {"sv3"}});
    EXPECT_EQ(request.url,
              "https://api.pokemontcg.io/v2/cards"
              "?q=nationalPokedexNumbers%3A6%20%28set.id%3Asv3%29&pageSize=250");
}

// A duplicated printed code resolves to several set ids, ORed inside the clause.
TEST(PokemonTcgIoApiTest, ResolvesSearchNarrowedToSeveralSetsWithOr) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({.dexNumber = 25, .setIds = {"cel25", "cel25c"}});
    EXPECT_EQ(
        request.url,
        "https://api.pokemontcg.io/v2/cards"
        "?q=nationalPokedexNumbers%3A25%20%28set.id%3Acel25%20OR%20set.id%3Acel25c%29"
        "&pageSize=250");
}

// The set-list endpoint used to build the in-memory code/id table.
TEST(PokemonTcgIoApiTest, ResolvesSetListEndpoint) {
    PokemonTcgIoApi api;
    EXPECT_EQ(api.resolveSets().url, "https://api.pokemontcg.io/v2/sets?pageSize=250");
}

}  // namespace
