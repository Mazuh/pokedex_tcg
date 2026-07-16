#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/domain/card_condition.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
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
    // the reference fields are trimmed. Both audit stamps are set to now(). Returns
    // the persisted copy with its freshly minted id.
    CardCopy create(PokemonDexNum pokemonDexNum, CardReference cardRef,
                    CardOwnership ownership, CardCondition condition,
                    std::optional<CardBinderId> binderId, std::string comments);

    // Edit a copy's condition and free-text comments, bumping updatedAt. Throws
    // CardCopyError if no copy has that id.
    void editDetails(const CardCopyId& id, CardCondition condition, std::string comments);

    // File the copy in a binder, or clear its binder when nullopt — bumping
    // updatedAt. Filing never touches the copy's ownership. Throws CardCopyError if
    // no copy has that id (a bad binder id is rejected by the storage FK).
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
};

}  // namespace pokedex
