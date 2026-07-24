#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_foil.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_rarity.h"
#include "core/domain/card_reference.h"
#include "core/domain/types.h"

namespace pokedex {

class CardCopyRepository;

// APP — raised when a copy operation is invalid, e.g. a create with a blank
// collector number, or an edit/remove targeting an id that no longer exists.
class CardCopyError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// APP — the verbs of the CardCopy root: create / edit / soft-remove / hard-delete,
// plus the read behind the owned-cards browser. Like BinderService it owns the two
// things the domain and storage layers deliberately do not — minting ids and
// reading the clock — both injectable so tests are deterministic (the defaults use
// a random UUID and the system clock).
class CardCopyService {
public:
    using Clock = std::function<Timestamp()>;
    using IdGenerator = std::function<CardCopyId()>;

    explicit CardCopyService(CardCopyRepository& repo, Clock clock = systemClock(),
                             IdGenerator idGenerator = uuidGenerator());

    // Record a new copy the user owns / is receiving. The collector number is the
    // card's printed identity and is required — a blank one throws CardCopyError;
    // the reference fields are trimmed. `pokemonDexNum` is optional: pass nullopt
    // for a card that depicts no species (a Trainer or Energy card). `rarity` and
    // `foil` are the card's optional rarity classification and foil treatment. Both
    // audit stamps are set to now(). Returns the persisted copy with its freshly
    // minted id.
    CardCopy create(std::optional<PokemonDexNum> pokemonDexNum, CardReference cardRef,
                    CardOwnership ownership, std::optional<CardCondition> condition,
                    std::optional<CardRarity> rarity, std::optional<CardFoil> foil,
                    std::optional<CardBinderId> binderId, std::string comments);

    // Edit a copy's mutable details — its printed reference (the copy's language is
    // the field the edit surface actually changes; the printing identity is otherwise
    // shown read-only), ownership, condition (optional), rarity (optional), foil
    // treatment (optional), and free-text comments — bumping updatedAt. The reference
    // is trimmed and a blank collector number is rejected, mirroring create(). Binder
    // filing is a separate verb (assignToBinder). Throws CardCopyError if no copy has
    // that id, or if the copy is soft-Removed — a removed copy is frozen history,
    // editable only by hardDelete.
    void editDetails(const CardCopyId& id, CardReference cardRef, CardOwnership ownership,
                     std::optional<CardCondition> condition, std::optional<CardRarity> rarity,
                     std::optional<CardFoil> foil, std::string comments);

    // File the copy in a binder, or clear its binder when nullopt — bumping
    // updatedAt. Filing never touches the copy's ownership. Throws CardCopyError if
    // no copy has that id (a bad binder id is rejected by the storage FK), or if the
    // copy is soft-Removed — frozen history is not refiled.
    void assignToBinder(const CardCopyId& id, std::optional<CardBinderId> binderId);

    // Soft-remove: mark the copy Removed — kept for auditable history rather than
    // deleted — bumping updatedAt. An optional `note` (trimmed) is appended to the
    // copy's comments on its own line, so the reason it left the collection (sold,
    // lost, traded…) stays on the record. Throws CardCopyError if no copy has that id.
    void remove(const CardCopyId& id, const std::string& note = "");

    // Permanently delete a copy row (something added by mistake). Throws
    // CardCopyError if no copy has that id.
    void hardDelete(const CardCopyId& id);

    // Every copy, oldest first — the owned-cards browser filters this itself.
    std::vector<CardCopy> listAll();

    // Every copy filed in `binderId`, in insertion order — the binder-scoped read
    // behind the binder guide's detail panel (which owned copy to show for a row).
    // Avoids scanning the whole inventory just to find one binder's copies.
    std::vector<CardCopy> listByBinder(const CardBinderId& binderId);

    // A monotonically increasing counter bumped on every mutation (create /
    // editDetails / assignToBinder / remove / hardDelete); reads never advance it. A
    // view that caches a derived read of the inventory can compare this against the
    // value it last loaded at to skip an expensive re-read when nothing has changed
    // since — a cheap "is my cached copy data stale?" check with no storage round-trip
    // (the Pokémon browser gates its full-inventory refresh on it). Because the service
    // is a single shared instance, a mutation in any section is visible to all.
    long revision() const noexcept { return revision_; }

    // The defaults, exposed so callers can wrap/compose them if needed.
    static Clock systemClock();
    static IdGenerator uuidGenerator();

private:
    // Load a copy by id or throw CardCopyError — the read-modify-write helper
    // behind editDetails / remove.
    CardCopy require(const CardCopyId& id);

    CardCopyRepository& repo_;
    Clock clock_;
    IdGenerator idGenerator_;
    // Bumped by every mutating verb; see revision().
    long revision_ = 0;
};

}  // namespace pokedex
