#include "core/app/uuid.h"

#include <array>
#include <cstdint>
#include <random>

namespace pokedex {

std::string newUuidV4() {
    // RFC 4122 version-4 UUID from a per-thread PRNG, seeded once.
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
}

}  // namespace pokedex
