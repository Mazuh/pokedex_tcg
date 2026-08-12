#include "core/app/binder_guide_service.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/domain/card_copy.h"
#include "core/domain/collection_status.h"
#include "core/domain/pokemon.h"
#include "core/domain/pokemon_catalog.h"
#include "core/storage/card_copy_repository.h"
#include "core/storage/wishlist_repository.h"

namespace pokedex {

namespace {

// The catalog species a dex number names, or nullptr when it names none. The catalog is
// contiguous over 1..N by dex number, so a valid number indexes straight in.
//
// This is deliberately ONE helper rather than the range check spelled out at each use.
// The guide's contract is that nothing filed here is invisible, and that rests on the
// partition below and the emit loop agreeing EXACTLY on which dex numbers resolve: if the
// partition kept a copy that the emit loop then skipped, that card would silently vanish
// from the guide — the very bug this row model exists to fix, and one no test would catch
// unless it probed the boundary.
const Pokemon* speciesAt(PokemonDexNum dexNum, std::span<const Pokemon> catalog) {
    if (dexNum < 1 || static_cast<std::size_t>(dexNum) > catalog.size()) {
        return nullptr;
    }
    return &catalog[static_cast<std::size_t>(dexNum) - 1];
}

// One filed copy's own standing. A copy row speaks for exactly one physical card,
// so its status is a direct reading of that card's ownership — no precedence runs
// between the copies of a species, since each gets its own row.
CollectionStatus statusOfCopy(CardOwnership ownership) {
    switch (ownership) {
        case CardOwnership::Incoming: return CollectionStatus::Incoming;
        case CardOwnership::Owned:    return CollectionStatus::Completed;
        case CardOwnership::Removed:  return CollectionStatus::Removed;
    }
    return CollectionStatus::Incomplete;
}

// A listed species' standing when it has NO copy filed in this binder, in the
// first-match-wins precedence CollectionStatus documents. Only three of the six
// cases can arise: Incoming / Completed / Removed each require a copy filed here,
// and such a species gets copy rows instead of a placeholder — so by construction
// they are unreachable from here.
CollectionStatus placeholderStatus(PokemonDexNum dexNum,
                                   const std::set<PokemonDexNum>& ownedElsewhere,
                                   const std::set<PokemonDexNum>& wished) {
    if (wished.contains(dexNum)) return CollectionStatus::Wished;
    if (ownedElsewhere.contains(dexNum)) return CollectionStatus::Elsewhere;
    return CollectionStatus::Incomplete;
}

}  // namespace

std::set<PokemonDexNum> listedSpecies(const CardBinder& binder,
                                      std::span<const CardBinderEntry> rows) {
    std::set<PokemonDexNum> listed;
    if (binder.pokemonRegions.empty()) {
        // No regions to derive a checklist from, so the rows ARE the checklist. Safe here
        // precisely because such a binder files nothing as an extra: with no scope to fall
        // outside of, every species-bearing row is one it lists.
        for (const CardBinderEntry& row : rows) {
            if (row.pokemon) {
                listed.insert(row.pokemon->dexNumber);
            }
        }
        return listed;
    }
    const std::set<Region> scoped(binder.pokemonRegions.begin(), binder.pokemonRegions.end());
    for (const Pokemon& pokemon : pokemonCatalog()) {
        if (scoped.contains(pokemon.region)) {
            listed.insert(pokemon.dexNumber);
        }
    }
    return listed;
}

CollectionStatus BinderGuideService::placeholderStatusFor(const CardBinderId& binderId,
                                                          PokemonDexNum dexNum) {
    const std::vector<PokemonDexNum> elsewhereList = copies_.ownedElsewhere(binderId);
    const std::vector<PokemonDexNum> wishedList = wishlist_.wishedDexNums();
    return placeholderStatus(dexNum, {elsewhereList.begin(), elsewhereList.end()},
                             {wishedList.begin(), wishedList.end()});
}

std::vector<CardBinderEntry> BinderGuideService::buildEntries(const CardBinder& binder) {
    const std::vector<CardCopy> copiesInBinder = copies_.listByBinder(binder.id);
    const std::vector<PokemonDexNum> elsewhereList = copies_.ownedElsewhere(binder.id);
    const std::vector<PokemonDexNum> wishedList = wishlist_.wishedDexNums();
    const std::set<PokemonDexNum> ownedElsewhere(elsewhereList.begin(), elsewhereList.end());
    const std::set<PokemonDexNum> wished(wishedList.begin(), wishedList.end());

    const std::span<const Pokemon> catalog = pokemonCatalog();

    // The binder's deliberate empty pockets, indexed by the row each sits before. A
    // blank names EITHER a species OR one copy, never both and never neither, and counts
    // at least one pocket; a row failing that can't be placed, so it is dropped rather
    // than guessed at (the same tolerance the repository's region decode has). Several
    // records at one anchor simply add up.
    std::map<PokemonDexNum, int> blanksBeforeDex;
    std::unordered_map<CardCopyId, int> blanksBeforeCopy;
    for (const CardBinderBlank& blank : binder.pocketBlanks) {
        if (blank.blanks <= 0 || blank.beforeDexNum.has_value() == blank.beforeCopyId.has_value()) {
            continue;
        }
        if (blank.beforeDexNum) {
            blanksBeforeDex[*blank.beforeDexNum] += blank.blanks;
        } else {
            blanksBeforeCopy[*blank.beforeCopyId] += blank.blanks;
        }
    }

    // The CHECKLIST: the species this binder holds a slot for, dex-ordered and deduplicated
    // by std::set (a species shared across two scoped regions appears once). It is derived
    // from the binder's regions ALONE, so that filing or moving a card can never add a slot
    // or take one away — which is what makes an album's pocket count stable enough to plan
    // a physical layout around.
    //
    // A binder with no regions recorded is the exception: it has no checklist to derive, so
    // it falls back to listing whatever is filed in it (below).
    const bool reserved = !binder.pokemonRegions.empty();
    std::set<PokemonDexNum> dexNums;
    if (reserved) {
        const std::set<Region> scoped(binder.pokemonRegions.begin(),
                                      binder.pokemonRegions.end());
        for (const Pokemon& pokemon : catalog) {
            if (scoped.contains(pokemon.region)) {
                dexNums.insert(pokemon.dexNumber);
            }
        }
    }

    // Partition the filed copies against that checklist. The repository returns them in
    // filed order (inserted_at, rowid), so push_back preserves that within a species, and
    // the std::map keeps the species themselves dex-ordered.
    //
    // Everything the checklist does NOT claim goes to the tail — the EXTRAS area at the end
    // of the binder. That covers a card depicting no species, a copy whose dex number
    // doesn't resolve, and a species outside the binder's regions: a Johto card in a
    // Kanto+Kalos album is not part of that album's Pokédex run, and wedging it in at #152
    // would shove every later region along by a pocket. Extras carry no dex ordering anyone
    // asked for, so they keep filed order and are arranged by hand from there.
    //
    // The pointers reference copiesInBinder, which outlives this whole function.
    std::map<PokemonDexNum, std::vector<const CardCopy*>> copiesByDex;
    std::vector<const CardCopy*> tail;
    std::unordered_map<CardCopyId, const CardCopy*> copyById;
    for (const CardCopy& copy : copiesInBinder) {
        copyById.emplace(copy.id, &copy);
        const bool onChecklist = copy.pokemonDexNum &&
                                 speciesAt(*copy.pokemonDexNum, catalog) != nullptr &&
                                 (!reserved || dexNums.contains(*copy.pokemonDexNum));
        if (onChecklist) {
            copiesByDex[*copy.pokemonDexNum].push_back(&copy);
        } else {
            tail.push_back(&copy);
        }
    }
    // The region-less fallback: with nothing to derive a checklist from, what is filed here
    // IS the checklist. Every such species resolved in the catalog above, so the emit loop's
    // guard below still holds.
    if (!reserved) {
        for (const auto& filed : copiesByDex) {
            dexNums.insert(filed.first);
        }
    }

    // Every dex number a blank or a placement may name and still be honoured: the checklist,
    // PLUS the species of any card that landed in the extras. An out-of-region species has
    // no checklist slot, but it does have a row, and the user may well have pinned a page
    // break ahead of it back when it sat in the dex run. Dropping those anchors would make
    // the gap silently stop rendering — and worse, would let a later move CANONICALISE it
    // away for good (canonicalBlankSets only spares a run whose anchor names no row at all).
    // Honouring them instead carries the layout across with the card.
    std::set<PokemonDexNum> anchorableDex = dexNums;
    std::set<PokemonDexNum> extrasSpecies;
    for (const CardCopy* copy : tail) {
        if (copy->pokemonDexNum && speciesAt(*copy->pokemonDexNum, catalog) != nullptr) {
            anchorableDex.insert(*copy->pokemonDexNum);
            extrasSpecies.insert(*copy->pokemonDexNum);
        }
    }

    // The cards the user pulled OUT of this derived order and pinned before some other
    // row. A placement names one copy filed here and AT MOST one anchor — naming both
    // halves points at no single row, while naming neither is the legal "at the very end"
    // case, the only way the last pocket is reachable.
    std::unordered_map<CardCopyId, const CardBinderPlacement*> placementByCopy;
    for (const CardBinderPlacement& placement : binder.cardPlacements) {
        if (placement.beforeDexNum && placement.beforeCopyId) {
            continue;  // unplaceable — dropped, like a malformed blank
        }
        if (!copyById.contains(placement.cardCopyId)) {
            continue;  // names a card that isn't filed here
        }
        placementByCopy.emplace(placement.cardCopyId, &placement);
    }

    // Honour only the placements whose anchor chain TERMINATES at a row this guide emits
    // on its own — a listed species, a filed copy that isn't itself placed, or the end.
    // Iterating to a fixed point is what rejects a CYCLE (in "X before Y before X",
    // neither anchor ever becomes reachable, so neither is honoured) as well as an orphan
    // (a species whose region was un-scoped, a card refiled elsewhere).
    //
    // A rejected placement is simply ignored and its copy renders in its natural dex
    // position. That fallback is load-bearing: this guide's contract is that nothing
    // filed here is invisible, so no arrangement — however broken, hand-edited or
    // circular — may cost the user a card.
    std::unordered_set<CardCopyId> placed;
    for (bool grew = true; grew;) {
        grew = false;
        for (const auto& [copyId, placement] : placementByCopy) {
            if (placed.contains(copyId)) {
                continue;
            }
            bool anchorEmits = true;  // neither anchor set: the end, which always exists
            if (placement->beforeDexNum) {
                anchorEmits = anchorableDex.contains(*placement->beforeDexNum);
            } else if (placement->beforeCopyId) {
                const CardCopyId& anchor = *placement->beforeCopyId;
                anchorEmits = copyById.contains(anchor) &&
                              (!placementByCopy.contains(anchor) || placed.contains(anchor));
            }
            if (anchorEmits) {
                placed.insert(copyId);
                grew = true;
            }
        }
    }

    // Group the honoured placements by the anchor they sit before. Ordinal orders those
    // sharing one anchor, nearest-last; the copy id breaks a tie so a hand-edited pair of
    // equal ordinals still renders deterministically rather than by hash order.
    std::map<PokemonDexNum, std::vector<const CardBinderPlacement*>> placedBeforeDex;
    std::unordered_map<CardCopyId, std::vector<const CardBinderPlacement*>> placedBeforeCopy;
    std::vector<const CardBinderPlacement*> placedAtEnd;
    for (const auto& [copyId, placement] : placementByCopy) {
        if (!placed.contains(copyId)) {
            continue;
        }
        if (placement->beforeDexNum) {
            placedBeforeDex[*placement->beforeDexNum].push_back(placement);
        } else if (placement->beforeCopyId) {
            placedBeforeCopy[*placement->beforeCopyId].push_back(placement);
        } else {
            placedAtEnd.push_back(placement);
        }
    }
    const auto byOrdinal = [](const CardBinderPlacement* a, const CardBinderPlacement* b) {
        return std::tie(a->ordinal, a->cardCopyId) < std::tie(b->ordinal, b->cardCopyId);
    };
    for (auto& [_, group] : placedBeforeDex) {
        std::sort(group.begin(), group.end(), byOrdinal);
    }
    for (auto& [_, group] : placedBeforeCopy) {
        std::sort(group.begin(), group.end(), byOrdinal);
    }
    std::sort(placedAtEnd.begin(), placedAtEnd.end(), byOrdinal);

    // The row count is no longer bounded by the catalog — a binder holding many
    // duplicates has more rows than it lists species.
    std::vector<CardBinderEntry> entries;
    entries.reserve(dexNums.size() + copiesInBinder.size());

    // Emit the blank pockets recorded before a row, if any. A blank row names neither a
    // species nor a copy and carries no status — see CardBinderEntry.
    const auto emitBlanksBefore = [&entries](const auto& counts, const auto& anchor) {
        const auto it = counts.find(anchor);
        if (it == counts.end()) {
            return;
        }
        for (int i = 0; i < it->second; ++i) {
            entries.push_back({std::nullopt, std::nullopt, std::nullopt});
        }
    };

    // One row for one filed copy, wherever it ends up sitting. Deriving the species from
    // the copy rather than from the loop it was reached through means a moved card and a
    // naturally ordered one can't be built differently.
    const auto pushCopyRow = [&entries, &placed, catalog](const CardCopy& copy) {
        const Pokemon* species =
            copy.pokemonDexNum ? speciesAt(*copy.pokemonDexNum, catalog) : nullptr;
        entries.push_back({species != nullptr ? std::optional<Pokemon>(*species) : std::nullopt,
                           copy.id, statusOfCopy(copy.ownership), placed.contains(copy.id)});
    };

    // Everything the user arranged to sit immediately before one row, in the order that
    // makes every target pocket expressible: first the cards MOVED there (each preceded by
    // whatever rides on it in turn), then the blank pockets recorded at that anchor.
    //
    // That order is the crux. A move rebalances the blank counts around the card it
    // places — the blanks that must precede the moved card are re-anchored onto the card
    // itself, and those that follow it stay on the anchor row — which only resolves to the
    // intended layout because a placement's own riders are emitted ahead of the anchor's.
    std::function<void(const CardCopyId&)> emitBefore;
    const auto emitPlacements = [&](const std::vector<const CardBinderPlacement*>* group) {
        if (group == nullptr) {
            return;
        }
        for (const CardBinderPlacement* placement : *group) {
            emitBefore(placement->cardCopyId);
            pushCopyRow(*copyById.at(placement->cardCopyId));
        }
    };
    emitBefore = [&](const CardCopyId& anchor) {
        const auto group = placedBeforeCopy.find(anchor);
        emitPlacements(group == placedBeforeCopy.end() ? nullptr : &group->second);
        // A copy anchor means "immediately before this row" wherever the row sits, so a
        // blank pinned to a moved card travels with it for free.
        emitBlanksBefore(blanksBeforeCopy, anchor);
    };
    const auto emitBeforeDex = [&](PokemonDexNum anchor) {
        const auto group = placedBeforeDex.find(anchor);
        emitPlacements(group == placedBeforeDex.end() ? nullptr : &group->second);
        emitBlanksBefore(blanksBeforeDex, anchor);
    };

    for (const PokemonDexNum dexNum : dexNums) {
        // The partition above routed every unresolvable filed copy to the tail through
        // this same helper, so a bucketed species always resolves here — this guard now
        // only covers a hypothetical bad region entry, whose number has no species to
        // show and is skipped rather than fabricated.
        const Pokemon* species = speciesAt(dexNum, catalog);
        if (species == nullptr) {
            continue;
        }
        const Pokemon& pokemon = *species;
        // What sits ahead of ALL this species' rows. Emitting it here — after the resolve
        // guard, before the placeholder branch — is deliberate on both counts: an anchor
        // on a dex number with no species is skipped along with the species it names
        // (never left floating), and a page break or a moved card can still be pinned
        // ahead of a species the user doesn't own a card of yet, which is the whole point
        // of pushing the next region onto a fresh page before collecting it.
        emitBeforeDex(dexNum);
        // One row per filed copy, in filed order — the copies themselves are what this
        // species has to say about this binder. A copy MOVED elsewhere is skipped here and
        // emitted at its anchor instead.
        //
        // `holding` counts only the copies actually OCCUPYING the sleeve, which is not the
        // same as the rows emitted: a soft-Removed copy keeps a row as frozen history but
        // holds no pocket (holdsPocket), so a species whose every copy was removed has an
        // empty sleeve and still needs its placeholder. Counting rows instead silently
        // dropped a reserved pocket on the app's ordinary delete, renumbering the Page and
        // Pocket columns for the whole rest of the binder.
        int holding = 0;
        const auto bucket = copiesByDex.find(dexNum);
        if (bucket != copiesByDex.end()) {
            for (const CardCopy* copy : bucket->second) {
                if (placed.contains(copy->id)) {
                    continue;
                }
                emitBefore(copy->id);
                pushCopyRow(*copy);
                holding += holdsPocket(entries.back()) ? 1 : 0;
            }
        }
        // A RESERVED slot stays reserved whatever the user files or moves. If nothing sits
        // in it — the species is uncollected, or its every copy was pinned to some other
        // pocket — it shows as a placeholder rather than vanishing, so the checklist keeps
        // a constant pocket count and the pages after it never shift underfoot.
        //
        // Moving a species' last copy out therefore reads as "that sleeve is empty again",
        // which is the point: two cards sharing a dex number (Kanto and Paldean Tauros both
        // being #128) are one Pokédex slot and two physical cards, and pinning one of them
        // elsewhere is exactly how the user says which is which.
        //
        // A region-less binder has no reserved slots — its species are listed only because
        // a card is filed under them — so there the species simply stops being listed.
        if (holding == 0 && reserved) {
            entries.push_back({pokemon, std::nullopt,
                               placeholderStatus(dexNum, ownedElsewhere, wished)});
        }
    }

    // The EXTRAS, last: cards the checklist doesn't claim, in filed order. A dex-anchored
    // blank or move on one of their species travels here with them (see anchorableDex),
    // emitted once, ahead of the first card of that species.
    std::set<PokemonDexNum> extrasAnchorsDone;
    for (const CardCopy* copy : tail) {
        // Ahead of the `placed` skip on purpose: if this species' only extras card was
        // itself pinned somewhere else, its dex-anchored gap still needs a home, and the
        // extras are where that species now belongs. Leaving it unemitted would let the
        // next move canonicalise it out of existence.
        if (copy->pokemonDexNum && extrasSpecies.contains(*copy->pokemonDexNum) &&
            extrasAnchorsDone.insert(*copy->pokemonDexNum).second) {
            emitBeforeDex(*copy->pokemonDexNum);
        }
        if (placed.contains(copy->id)) {
            continue;
        }
        emitBefore(copy->id);
        pushCopyRow(*copy);
    }
    // Cards pinned past every row. Blanks have no such anchor (one is always required),
    // so only placements land here — and they must, since every other target is phrased
    // as "before some row" and the last pocket has nothing after it to name.
    emitPlacements(&placedAtEnd);
    return entries;
}

}  // namespace pokedex
