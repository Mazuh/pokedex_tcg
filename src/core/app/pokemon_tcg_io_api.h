#pragma once

#include "core/app/card_catalog_api.h"

namespace pokedex {

// APP — the default CardCatalogApi implementation, backed by pokemontcg.io's
// public REST API (https://api.pokemontcg.io/v2). This is the one place that
// knows that API's URL scheme and Lucene-style query syntax: species search keys
// on `nationalPokedexNumbers`, and set narrowing ORs `set.id` clauses (never
// `set.ptcgoCode`, whose search index is unreliable). No API key is required for
// the anonymous rate tier. Pure and Qt-free — it only builds URLs.
class PokemonTcgIoApi : public CardCatalogApi {
public:
    HttpRequest resolveSearch(const CardSearchQuery& query) const override;
    HttpRequest resolveCardById(const std::string& cardId) const override;
    HttpRequest resolveSets() const override;
};

}  // namespace pokedex
