#include "core/app/binder_service.h"

#include <utility>

#include "core/app/uuid.h"
#include "core/storage/card_binder_repository.h"
#include "core/strings.h"

namespace pokedex {

namespace {

std::string requireName(const std::string& raw) {
    std::string name = trim(raw);
    if (name.empty()) {
        throw BinderError("A binder needs a name.");
    }
    return name;
}

// The physical layout is optional, but a recorded one has to be a real album: zero or
// negative sides describe nothing and would divide by zero when the guide pages by them.
void requireLayout(const std::optional<int>& capacity,
                   const std::optional<CardBinderPocketGrid>& pocketGrid) {
    if (capacity && *capacity <= 0) {
        throw BinderError("A binder's capacity must be at least one card.");
    }
    if (pocketGrid && (pocketGrid->rows <= 0 || pocketGrid->columns <= 0)) {
        throw BinderError("A binder's pocket grid needs at least one row and one column.");
    }
}

void requireBlank(const CardBinderBlank& blank) {
    if (blank.beforeDexNum.has_value() == blank.beforeCopyId.has_value()) {
        throw BinderError(
            "A blank pocket must sit before exactly one row — either a species or a card.");
    }
    if (blank.blanks <= 0) {
        throw BinderError("A run of blank pockets needs at least one pocket.");
    }
}

// Read a binder back after a write, so every mutating verb hands the caller exactly
// what storage now holds (see the class docstring). A missing binder means the id was
// wrong or it was removed underneath us — a caller error either way.
CardBinder reread(CardBinderRepository& repo, const CardBinderId& id) {
    std::optional<CardBinder> persisted = repo.find(id);
    if (!persisted) {
        throw BinderError("no binder with id: " + id);
    }
    return std::move(*persisted);
}

}  // namespace

BinderService::Clock BinderService::systemClock() {
    return [] { return std::chrono::system_clock::now(); };
}

BinderService::IdGenerator BinderService::uuidGenerator() {
    return [] { return newUuidV4(); };
}

BinderService::BinderService(CardBinderRepository& repo, Clock clock, IdGenerator idGenerator)
    : repo_(repo), clock_(std::move(clock)), idGenerator_(std::move(idGenerator)) {}

CardBinder BinderService::create(std::string name, std::vector<Region> regions,
                                 std::optional<int> capacity,
                                 std::optional<CardBinderPocketGrid> pocketGrid) {
    requireLayout(capacity, pocketGrid);
    const Timestamp now = clock_();
    CardBinder binder;
    binder.id = idGenerator_();
    binder.name = requireName(name);
    binder.pokemonRegions = std::move(regions);
    binder.capacity = capacity;
    binder.pocketGrid = pocketGrid;
    binder.insertedAt = now;
    binder.updatedAt = now;
    repo_.add(binder);
    return binder;
}

CardBinder BinderService::update(const CardBinderId& id, std::string name,
                                 std::vector<Region> regions, std::optional<int> capacity,
                                 std::optional<CardBinderPocketGrid> pocketGrid) {
    requireLayout(capacity, pocketGrid);
    repo_.update(id, requireName(name), regions, capacity, pocketGrid, clock_());
    return reread(repo_, id);
}

CardBinder BinderService::insertBlanks(const CardBinderId& id, const CardBinderBlank& blank) {
    requireBlank(blank);
    repo_.addBlanks(id, blank);
    return reread(repo_, id);
}

CardBinder BinderService::removeBlanks(const CardBinderId& id, const CardBinderBlank& blank) {
    requireBlank(blank);
    repo_.removeBlanks(id, blank);
    return reread(repo_, id);
}

void BinderService::remove(const CardBinderId& id) { repo_.remove(id); }

std::vector<CardBinder> BinderService::list() { return repo_.listAll(); }

}  // namespace pokedex
