#include "core/app/binder_service.h"

#include <utility>

#include "core/app/uuid.h"
#include "core/storage/card_binder_repository.h"

namespace pokedex {

namespace {

// Trim surrounding ASCII whitespace. Binder names are user-entered, so a value
// that is blank or only spaces is rejected rather than stored verbatim.
std::string trim(const std::string& s) {
    const char* ws = " \t\n\r\f\v";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

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

CardBinder BinderService::create(std::string name, std::optional<Region> region) {
    const Timestamp now = clock_();
    CardBinder binder;
    binder.id = idGenerator_();
    binder.name = requireName(name);
    binder.pokemonRegion = region;
    binder.insertedAt = now;
    binder.updatedAt = now;
    repo_.add(binder);
    return binder;
}

void BinderService::rename(const CardBinderId& id, std::string newName) {
    repo_.updateName(id, requireName(newName), clock_());
}

void BinderService::remove(const CardBinderId& id) { repo_.remove(id); }

std::vector<CardBinder> BinderService::list() { return repo_.listAll(); }

}  // namespace pokedex
