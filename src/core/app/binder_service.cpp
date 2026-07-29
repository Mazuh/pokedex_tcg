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

}  // namespace

BinderService::Clock BinderService::systemClock() {
    return [] { return std::chrono::system_clock::now(); };
}

BinderService::IdGenerator BinderService::uuidGenerator() {
    return [] { return newUuidV4(); };
}

BinderService::BinderService(CardBinderRepository& repo, Clock clock, IdGenerator idGenerator)
    : repo_(repo), clock_(std::move(clock)), idGenerator_(std::move(idGenerator)) {}

CardBinder BinderService::create(std::string name, std::vector<Region> regions) {
    const Timestamp now = clock_();
    CardBinder binder;
    binder.id = idGenerator_();
    binder.name = requireName(name);
    binder.pokemonRegions = std::move(regions);
    binder.insertedAt = now;
    binder.updatedAt = now;
    repo_.add(binder);
    return binder;
}

void BinderService::update(const CardBinderId& id, std::string name,
                           std::vector<Region> regions) {
    repo_.update(id, requireName(name), regions, clock_());
}

void BinderService::remove(const CardBinderId& id) { repo_.remove(id); }

std::vector<CardBinder> BinderService::list() { return repo_.listAll(); }

}  // namespace pokedex
