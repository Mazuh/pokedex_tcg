#pragma once

// Umbrella header and overview of the Pokédex TCG domain model.
//
// The model splits into three zones:
//
//   CATALOG    — authoritative-but-fixed reference data, shipped as compile-time
//                constants/enums with no persistence: Region, Pokemon.
//
//   COLLECTION — the user's mutable source of truth, the only data ever stored:
//                CardBinder, CardCopy, Wishlist. Each carries flat
//                insertedAt/updatedAt UTC audit stamps.
//
//   INFERRED   — pure functions of the source of truth, never stored and never
//                mutated, only recomputed: CollectionStatus and the
//                CardBinderEntry projection.
//
// CardReference is a shared value object (a printing's printed identity)
// embedded in CardCopy. There is intentionally no Card entity: a full card
// catalog is an external, fetch-and-cache concern, not something this domain
// stores or manages.
//
// Cross-cutting rules:
//   * References across entities are by id/value (PokemonDexNum, CardReference,
//     CardBinderId), never by pointer — each entity stays independently
//     constructible, testable, and later serializable.
//   * The domain never reads the clock; the app layer supplies timestamps.
//   * Behaviour (buying, removing, buildBinderEntries, …) lives in the app
//     layer, not on these types — entities are nouns, services are verbs.

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"
#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "core/domain/collection_status.h"
#include "core/domain/pokemon.h"
#include "core/domain/region.h"
#include "core/domain/types.h"
#include "core/domain/wishlist.h"
