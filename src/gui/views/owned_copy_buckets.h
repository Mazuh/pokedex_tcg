#pragma once

#include <QString>

#include <algorithm>
#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "core/domain/types.h"
#include "gui/views/pokemon_detail_panel.h"
#include "gui/views/price_labels.h"  // filterSuppressed

namespace pokedex {

// GUI — bucket owned, species-tied copies by dex number: the single definition of
// "which copies drive the detail panel's copy mode". Both the binder guide
// (BinderView, over one binder's copies) and the Pokémon browser (PokemonListView,
// over every binder's copies) feed their copy list through here so the two views
// can never drift on the predicate — only Owned copies that depict a species
// (pokemonDexNum set) qualify, mirroring the "Completed" status and the Owned
// column's count. Species-free copies (Trainer/Energy) never appear in a
// species-oriented projection.
inline std::unordered_map<PokemonDexNum, std::vector<CardCopy>> bucketOwnedCopiesByDex(
    const std::vector<CardCopy>& copies) {
    std::unordered_map<PokemonDexNum, std::vector<CardCopy>> byDex;
    for (const CardCopy& copy : copies) {
        if (copy.pokemonDexNum && copy.ownership == CardOwnership::Owned) {
            byDex[*copy.pokemonDexNum].push_back(copy);
        }
    }
    return byDex;
}

// GUI — locate the copy the detail panel is showing within a bucketed-by-dex map:
// the copy with id `copyId` filed under species `dex`, or nullptr if the species has
// no bucket or no copy with that id. Both copy-mode hosts (the Pokémon browser over
// `owned_`, a binder guide over `ownedHere_`) run this identical guard before opening
// the edit page, so it lives here rather than duplicated in each openEditCopy. The
// returned pointer is valid only until the map is next rebuilt.
inline const CardCopy* findOwnedCopy(
    const std::unordered_map<PokemonDexNum, std::vector<CardCopy>>& byDex, int dex,
    const QString& copyId) {
    const auto it = byDex.find(dex);
    if (it == byDex.end()) {
        return nullptr;
    }
    const std::string id = copyId.toStdString();
    const auto copyIt = std::find_if(it->second.begin(), it->second.end(),
                                     [&](const CardCopy& c) { return c.id == id; });
    return copyIt == it->second.end() ? nullptr : &*copyIt;
}

// GUI — after an in-panel Fetch auto-resolved a copy's catalog link, write the new
// external card id back into the copy with id `copyId` inside a plain vector<CardCopy>, so
// a later re-selection renders it as linked (and value stats that key on externalCardId
// include it) instead of re-running the resolve. Returns whether the copy was found (so a
// caller scanning several vectors can stop). Both the binder guide (over its filedCopies_)
// and My Cards (over its loaded_) keep such a flat cache and learn the id from the panel's
// cardLinked signal, so this find-by-id write lives here rather than hand-spelled per view.
inline bool applyLinkedCardToVector(std::vector<CardCopy>& copies, const QString& copyId,
                                    const QString& externalCardId) {
    const std::string id = copyId.toStdString();
    for (CardCopy& copy : copies) {
        if (copy.id == id) {
            copy.externalCardId = externalCardId.toStdString();
            return true;
        }
    }
    return false;
}

// GUI — the bucketed-by-dex twin: write the linked id into whichever bucket holds the copy,
// delegating each bucket to applyLinkedCardToVector so the find-by-id write is defined once.
// No-op when the id isn't in the map. Both copy-mode hosts (the browser over `owned_`, a
// binder guide over `ownedHere_`) call this from the panel's cardLinked signal.
inline void applyLinkedCardToBuckets(
    std::unordered_map<PokemonDexNum, std::vector<CardCopy>>& byDex, const QString& copyId,
    const QString& externalCardId) {
    for (auto& [dex, copies] : byDex) {
        if (applyLinkedCardToVector(copies, copyId, externalCardId)) {
            return;
        }
    }
}

// GUI — drive the detail panel for species `dex` from a bucketed-by-dex copy map:
// copy mode (showing `preferCopyId`, else a random copy) when the species owns copies
// on this surface, plain artwork otherwise. This owned_-lookup → showPokemon dispatch
// was identical in both copy-mode hosts — the Pokémon browser over `owned_`, a binder
// guide over `ownedHere_` — so it lives here rather than duplicated in each view's
// showSpeciesInPanel, keeping the two from drifting on the copy-mode entry condition.
// (bucketOwnedCopiesByDex never stores an empty vector, so a present bucket always has
// a copy; the emptiness check is belt-and-suspenders.)
inline void showSpeciesCopiesInPanel(
    PokemonDetailPanel* panel,
    const std::unordered_map<PokemonDexNum, std::vector<CardCopy>>& byDex, int dex,
    const QString& name, const QString& preferCopyId = QString()) {
    const auto it = byDex.find(dex);
    if (it != byDex.end() && !it->second.empty()) {
        panel->showPokemon(dex, name, it->second, preferCopyId);
    } else {
        panel->showPokemon(dex, name);
    }
}

// GUI — the cached market prices for a copy: the rows keyed by its externalCardId in a
// by-external-id price map, or an empty list when the copy is unlinked (no external id) or
// nothing is cached for it. Both card tables (My Cards, the binder guide) keep such a
// batched cache — filled once per reload so a header-sort never re-queries — and read it
// through here, so the lookup can't drift between the two views.
inline const std::vector<CardPrice>& pricesForCopy(
    const std::unordered_map<std::string, std::vector<CardPrice>>& byExternalId,
    const CardCopy& copy) {
    static const std::vector<CardPrice> kEmpty;
    if (copy.externalCardId.empty()) {
        return kEmpty;
    }
    const auto it = byExternalId.find(copy.externalCardId);
    return it == byExternalId.end() ? kEmpty : it->second;
}

// GUI — a copy's cached prices with its suppressed vendors already filtered out, so a hidden
// vendor never reaches a table's Prices cell, its per-copy value, or its price-sort key — the
// same filtering the detail panel's headline applies. `suppressedByExternalId` is keyed by the
// same external card id as the price map (a card with none is absent). Returns a REFERENCE to the
// cached rows with no copy in the common case (an empty suppression map — nobody has hidden a
// vendor — or a copy with no suppression); only a card that actually has a suppression fills
// `scratch` with the filtered rows and returns a reference to that. `scratch` must outlive the
// returned reference; callers keep one per loop and reuse it, so a whole table rebuild allocates
// only for the (rare) suppressed rows, not once per priced row.
inline const std::vector<CardPrice>& visiblePricesForCopy(
    const std::unordered_map<std::string, std::vector<CardPrice>>& byExternalId,
    const std::unordered_map<std::string, std::vector<std::string>>& suppressedByExternalId,
    const CardCopy& copy, std::vector<CardPrice>& scratch) {
    const std::vector<CardPrice>& all = pricesForCopy(byExternalId, copy);
    if (suppressedByExternalId.empty() || copy.externalCardId.empty()) {
        return all;
    }
    const auto it = suppressedByExternalId.find(copy.externalCardId);
    if (it == suppressedByExternalId.end()) {
        return all;
    }
    scratch = filterSuppressed(all, it->second);
    return scratch;
}

// GUI — whether any copy in `copies` is linked to `externalCardId` (a QString straight from the
// app-wide pricesReady signal). Both card tables gate that signal through it so a fetch for a card
// they don't hold — most fetches — is ignored instead of re-reading the cache and rebuilding the
// whole table on every fetch anywhere.
inline bool anyCopyLinkedTo(const std::vector<CardCopy>& copies, const QString& externalCardId) {
    const std::string id = externalCardId.toStdString();
    return std::any_of(copies.begin(), copies.end(),
                       [&](const CardCopy& c) { return c.externalCardId == id; });
}

// GUI — the distinct linked (non-blank) external card ids among the copies matching `include`,
// deduplicated in first-seen order. The single id-gathering pass both batched loads below feed to
// their one query, so a reload is not N round-trips; extracted so the include/dedup rule can't
// drift between the price load and the suppression load.
template <typename Predicate>
inline std::vector<std::string> distinctExternalIds(const std::vector<CardCopy>& copies,
                                                    Predicate include) {
    std::vector<std::string> ids;
    std::unordered_set<std::string> seen;
    for (const CardCopy& copy : copies) {
        if (include(copy) && !copy.externalCardId.empty() &&
            seen.insert(copy.externalCardId).second) {
            ids.push_back(copy.externalCardId);
        }
    }
    return ids;
}

// GUI — the by-external-id suppression map both card tables load alongside the price map: the
// hidden vendors of every distinct linked id among the copies matching `include`, read in ONE
// batched query (no network) so a reload is not N round-trips and a header sort never re-queries.
// Best-effort — a storage failure yields an empty map (nothing filtered). `Lookup` needs
// `suppressedVendorsMany(const std::vector<std::string>&)`.
template <typename Lookup, typename Predicate>
inline std::unordered_map<std::string, std::vector<std::string>> loadSuppressedVendorsFor(
    Lookup& lookup, const std::vector<CardCopy>& copies, Predicate include) {
    try {
        return lookup.suppressedVendorsMany(distinctExternalIds(copies, include));
    } catch (const std::exception&) {
        return {};
    }
}

// GUI — the by-external-id price map both card tables (My Cards, the binder guide) hold: the
// distinct linked ids of the copies matching `include` (skipping unlinked ones), read in ONE
// batched cache query (cachedMany, no network) so a reload is not N round-trips and a header
// sort never re-queries. Best-effort — a storage failure yields an empty map (blank Prices
// cells) rather than crashing. Templated on the lookup service and the predicate so the two
// views (differing only in which copies count — Owned vs non-Removed) share one definition.
// `Lookup` needs `cachedMany(const std::vector<std::string>&)`.
template <typename Lookup, typename Predicate>
inline std::unordered_map<std::string, std::vector<CardPrice>> loadCachedPricesFor(
    Lookup& lookup, const std::vector<CardCopy>& copies, Predicate include) {
    try {
        return lookup.cachedMany(distinctExternalIds(copies, include));
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace pokedex
