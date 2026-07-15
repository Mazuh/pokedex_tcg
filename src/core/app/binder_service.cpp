#include "core/app/binder_service.h"

#include <array>
#include <cstdint>
#include <random>
#include <utility>

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
    return [] {
        // RFC 4122 version-4 UUID from a per-thread PRNG, seeded once. Randomness
        // (unlike the clock) is fine to draw here in the app layer; it only needs
        // to be unique, not reproducible.
        static thread_local std::mt19937_64 rng(std::random_device{}());
        std::uniform_int_distribution<std::uint64_t> dist;
        std::uint64_t hi = dist(rng);
        std::uint64_t lo = dist(rng);
        hi = (hi & ~(std::uint64_t{0xF} << 12)) | (std::uint64_t{0x4} << 12);  // version 4
        lo = (lo & ~(std::uint64_t{0x3} << 62)) | (std::uint64_t{0x2} << 62);  // variant 1

        static constexpr std::array<char, 16> kHex{'0', '1', '2', '3', '4', '5', '6', '7',
                                                    '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
        char buf[36];
        int pos = 0;
        auto emit = [&](std::uint64_t value, int nibbles) {
            for (int i = nibbles - 1; i >= 0; --i) {
                buf[pos++] = kHex[(value >> (i * 4)) & 0xF];
            }
        };
        emit(hi >> 32, 8);
        buf[pos++] = '-';
        emit(hi >> 16, 4);
        buf[pos++] = '-';
        emit(hi, 4);
        buf[pos++] = '-';
        emit(lo >> 48, 4);
        buf[pos++] = '-';
        emit(lo, 12);
        return std::string(buf, sizeof(buf));
    };
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
