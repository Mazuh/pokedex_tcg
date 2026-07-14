#pragma once

namespace pokedex {

// INFERRED — a Pokémon's standing as seen from within one CardBinder. Computed,
// never stored. Resolved in FIRST-MATCH-WINS precedence order (top to bottom);
// the first satisfied case is the result. Declared in that order:
//
//   Incoming   — >=1 Incoming copy filed in this binder
//   Completed  — >=1 Owned    copy filed in this binder
//   Wished     — >=1 wishlist source for this Pokémon (global, not binder-scoped)
//   Elsewhere  — >=1 Owned    copy that is NOT filed in this binder
//   Removed    — >=1 Removed  copy that still carries this binder's id
//   Incomplete — none of the above (default)
//
// So an arriving card outranks one already owned here, and owning a copy in
// another binder still reads as available ("Elsewhere") rather than missing.
enum class CollectionStatus {
    Incoming,
    Completed,
    Wished,
    Elsewhere,
    Removed,
    Incomplete,
};

}  // namespace pokedex
