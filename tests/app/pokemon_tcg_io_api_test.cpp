#include "core/app/pokemon_tcg_io_api.h"

#include <gtest/gtest.h>

#include "core/app/card_catalog_api.h"

namespace {

using pokedex::PokemonTcgIoApi;

// A plain species search: the national-dex clause, URL-encoded (the colon
// becomes %3A), at the API's max page size.
TEST(PokemonTcgIoApiTest, ResolvesSpeciesSearchByDexNumber) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({6, {}});
    EXPECT_EQ(request.url,
              "https://api.pokemontcg.io/v2/cards"
              "?q=nationalPokedexNumbers%3A6&pageSize=250");
}

// Narrowing to one set appends a parenthesized set.id clause (set.id, never the
// unreliable ptcgoCode). Spaces/colons/parens are percent-encoded; the dot in
// "set.id" is unreserved and survives.
TEST(PokemonTcgIoApiTest, ResolvesSearchNarrowedToOneSet) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({6, {"sv3"}});
    EXPECT_EQ(request.url,
              "https://api.pokemontcg.io/v2/cards"
              "?q=nationalPokedexNumbers%3A6%20%28set.id%3Asv3%29&pageSize=250");
}

// A duplicated printed code resolves to several set ids, ORed inside the clause.
TEST(PokemonTcgIoApiTest, ResolvesSearchNarrowedToSeveralSetsWithOr) {
    PokemonTcgIoApi api;
    const auto request = api.resolveSearch({25, {"cel25", "cel25c"}});
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
