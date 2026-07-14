#pragma once

namespace pokedex {

// COLLECTION — lifecycle state of a physical CardCopy.
//
//   Incoming — bought, in transit, not yet in hand.
//   Owned    — physically held.
//   Removed  — soft-deleted (sold / lost / given away) but kept for auditable
//              history. A hard delete drops the record entirely and is an
//              app-layer operation, not a state here.
enum class CardOwnership {
    Incoming,
    Owned,
    Removed,
};

}  // namespace pokedex
