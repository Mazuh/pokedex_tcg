#pragma once

namespace pokedex {

// INFERRED — a Pokémon's standing as seen from within one CardBinder. Computed,
// never stored. Resolved in FIRST-MATCH-WINS precedence order (top to bottom);
// the first satisfied case is the result. Declared in that order:
//
//   Incoming   — [per copy] an Incoming copy filed in this binder
//   Completed  — [per copy] an Owned    copy filed in this binder
//   Wished     — >=1 wishlist source for this Pokémon (global, not binder-scoped)
//   Elsewhere  — >=1 Owned    copy that is NOT filed in this binder
//   Removed    — [per copy] a Removed  copy that still carries this binder's id
//   Incomplete — none of the above (default)
//
// The three marked [per copy] are read off ONE card, not quantified over a species —
// see the binder-guide note below, which is where that distinction bites.
//
// So an arriving card outranks one already owned here, and owning a copy in
// another binder still reads as available ("Elsewhere") rather than missing.
//
// A binder's guide splits those six across its two kinds of row (see
// CardBinderEntry). The three copy-bearing cases — Incoming, Completed, Removed —
// are resolved PER FILED COPY, since every filed copy gets its own row and carries
// its own ownership; no precedence runs between them. The remaining three —
// Wished, Elsewhere, Incomplete — resolve the PLACEHOLDER row of a species that
// has nothing filed here, and it is there that the first-match-wins order above
// still applies. The declaration order also doubles as the Status column's sort
// rank in the guide.
enum class CollectionStatus {
    Incoming,
    Completed,
    Wished,
    Elsewhere,
    Removed,
    Incomplete,
};

}  // namespace pokedex
