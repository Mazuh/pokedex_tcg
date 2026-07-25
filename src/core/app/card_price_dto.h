#pragma once

#include <string>

#include "core/domain/types.h"

namespace pokedex {

// The provenance tokens. `manual` is a hand-entered price — the discriminator that a
// refetch preserves (it replaces only API-sourced rows). The two vendor tokens are the
// pokemontcg.io block names, carried through verbatim; they live here as constants so
// the parser that EMITS a provenance and the GUI that MATCHES one can't drift apart.
inline constexpr char kManualPriceProvenance[] = "manual";
inline constexpr char kTcgplayerProvenance[] = "tcgplayer";
inline constexpr char kCardmarketProvenance[] = "cardmarket";

// APP — one price observation for a card, keyed by `externalCardId` — a
// source-neutral card identity, NOT by any owned CardCopy: a card's price lives
// independent of whether we hold a copy, so deleting a copy never removes pricing
// and a card can carry many observations at once. There is no single true price —
// a card is quoted by several vendors, in several variants, under several metrics
// (low/mid/high/market/trend) — so each such number is its own row and the caller
// shows the spread.
//
// `provenance` is the source: a vendor block name ("tcgplayer"/"cardmarket") or
// `kManualPriceProvenance`. `variant` and `metric` are pass-through labels that
// locate the number within a vendor — e.g. variant "holofoil"/"normal" (empty for
// cardmarket and manual, which have no per-variant split) and metric
// "market"/"low"/"trendPrice" (empty for a single manual price). They are plain
// strings, not enums: the vendor's vocabulary is open-ended and we never branch on
// it, only display it. `amountCents` is money as integer minor units (no float
// drift); `currency` is the ISO code the source quotes in ("USD" for tcgplayer,
// "EUR" for cardmarket, caller's choice for manual). `observedAt` is when the
// SOURCE last updated the price (the vendor's printed date, or the entry time for a
// manual price) — distinct from card_price_fetch's "when WE fetched". `id` is a
// uuid assigned when the row is persisted (empty on a freshly parsed row).
//
// `externalCardId` is deliberately named for what it IS — some external card
// catalog's stable id for the card — not for any one provider, so the storage
// stays source-agnostic. TODAY that id is always a pokemontcg.io card id (the
// format "sv3-125": setId + "-" + collector number), because pokemontcg.io is the
// only price source wired up; a different catalog would file its prices under its
// own id here (and, if its id scheme differs, needs a mapping to reach the same
// key). The column is `card_price.external_card_id`.
struct CardPrice {
    std::string id;
    // The external card catalog's id for this card — currently always a
    // pokemontcg.io card id ("sv3-125"). See the struct note.
    std::string externalCardId;
    std::string provenance;
    std::string variant;
    std::string metric;
    long long amountCents = 0;
    std::string currency;
    Timestamp observedAt{};
    std::string note;
};

}  // namespace pokedex
