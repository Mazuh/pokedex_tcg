#pragma once

#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

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
class BinderService {
public:
    using Clock = std::function<Timestamp()>;
    using IdGenerator = std::function<CardBinderId()>;

    explicit BinderService(CardBinderRepository& repo, Clock clock = systemClock(),
                           IdGenerator idGenerator = uuidGenerator());

    // Create a binder, optionally scoped to one or more regions (empty = none).
    // The name is trimmed; a blank name throws BinderError. Both audit stamps are
    // set to now(). Returns the persisted binder (with its freshly minted id).
    CardBinder create(std::string name, std::vector<Region> regions);

    // Edit an existing binder's name and region set. Trims/validates the name (a
    // blank name throws BinderError) and bumps updatedAt to now(). Unlike the old
    // rename, the regions can be changed here — the edit screen exposes both.
    void update(const CardBinderId& id, std::string name, std::vector<Region> regions);

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
