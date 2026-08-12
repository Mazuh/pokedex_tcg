#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/app/binder_move_planner.h"
#include "core/domain/card_binder.h"
#include "core/domain/region.h"
#include "core/domain/types.h"

namespace pokedex {

class CardBinderRepository;

// APP — raised when a binder operation is invalid, e.g. a create/rename with a
// blank name.
class BinderError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// APP — the verbs of the CardBinder root: the create / update / remove / list
// use cases behind the binders screen. It owns the two things the domain and
// storage layers deliberately do not: minting ids and reading the clock. Both
// are injectable so tests are deterministic; the defaults use a random UUID and
// the system clock.
//
// Every mutating verb RETURNS the persisted binder, read back through storage. The
// GUI keeps a by-value CardBinder for the binder it is showing and never re-reads it,
// so returning the whole entity lets a caller replace its copy wholesale
// (`binder_ = service.insertBlanks(...)`) instead of patching the fields it happens to
// know about — which is what keeps a newly added field from being silently dropped on
// that path.
class BinderService {
public:
    using Clock = std::function<Timestamp()>;
    using IdGenerator = std::function<CardBinderId()>;

    explicit BinderService(CardBinderRepository& repo, Clock clock = systemClock(),
                           IdGenerator idGenerator = uuidGenerator());

    // Create a binder, optionally scoped to one or more regions (empty = none) and
    // optionally recording the physical album's capacity and pocket grid (nullopt =
    // not recorded). The name is trimmed; a blank name, a non-positive capacity, or a
    // grid with a non-positive side throws BinderError. Both audit stamps are set to
    // now(). Returns the persisted binder (with its freshly minted id).
    CardBinder create(std::string name, std::vector<Region> regions,
                      std::optional<int> capacity = std::nullopt,
                      std::optional<CardBinderPocketGrid> pocketGrid = std::nullopt);

    // Edit an existing binder's name, region set and physical layout. Same validation as
    // create; bumps updatedAt to now(). The binder's blank pockets are NOT part of this
    // form and are left alone (see CardBinderRepository::update).
    //
    // The REGIONS may only change while the binder is still EMPTY. They decide which
    // species get a reserved slot, and hence where every page break falls, so re-scoping
    // an album that already holds cards has no sound answer (where would a newly added
    // second region begin, with 200 cards already filed against the first?) and would
    // orphan the blanks and moves arranged against the old layout. An empty binder has
    // none of that to lose, so a scope typed wrong at creation stays correctable rather
    // than forcing a delete-and-refile. Requesting a change on a non-empty binder throws;
    // passing the regions it already has always succeeds.
    CardBinder update(const CardBinderId& id, std::string name, std::vector<Region> regions,
                      std::optional<int> capacity = std::nullopt,
                      std::optional<CardBinderPocketGrid> pocketGrid = std::nullopt);

    // Whether this binder's regions can still be changed — nothing filed in or arranged
    // about it yet. The edit page asks so it can enable or disable its region checkboxes.
    bool canChangeRegions(const CardBinderId& id);

    // Leave `blank.blanks` pockets empty immediately before the row its anchor names,
    // pushing everything after it further along the page — the user-driven page break.
    // Inserting again at the same anchor widens the existing gap. Throws BinderError on
    // an anchor that doesn't name exactly one row, or a non-positive count.
    CardBinder insertBlanks(const CardBinderId& id, const CardBinderBlank& blank);

    // Take those pockets back; a no-op at an anchor that holds none. Same validation.
    CardBinder removeBlanks(const CardBinderId& id, const CardBinderBlank& blank);

    // Commit an arrangement worked out by planCardMove or planCardReset: the card's new
    // placement (or its removal) plus every blank run it rebalances, written as one
    // transaction. Both verbs commit through here, so there is a single write path.
    //
    // The plan is passed whole rather than recomputed here on purpose — the caller has
    // already shown the user what it will do (BinderMovePlan::shiftedCards), so
    // re-deriving it might commit something other than what was agreed to.
    CardBinder applyMove(const CardBinderId& id, const BinderMovePlan& plan);

    // Stop tracking a binder. Its filed copies are untouched (their binder link
    // is cleared by the database), per the "remove doesn't delete cards" rule.
    void remove(const CardBinderId& id);

    // All binders, oldest first — the list backing the GUI.
    std::vector<CardBinder> list();

    // The defaults, exposed so callers can wrap/compose them if needed.
    static Clock systemClock();
    static IdGenerator uuidGenerator();

private:
    CardBinderRepository& repo_;
    Clock clock_;
    IdGenerator idGenerator_;
};

}  // namespace pokedex
