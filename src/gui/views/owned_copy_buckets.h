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
    std::vector<std::string> ids;
    std::unordered_set<std::string> seen;
    for (const CardCopy& copy : copies) {
        if (include(copy) && !copy.externalCardId.empty() &&
            seen.insert(copy.externalCardId).second) {
            ids.push_back(copy.externalCardId);
        }
    }
    try {
        return lookup.cachedMany(ids);
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace pokedex
