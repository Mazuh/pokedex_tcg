#pragma once

#include <chrono>

#include "core/domain/types.h"

namespace pokedex {

// APP — the one freshness rule shared by every TTL'd local cache (the /v2/sets set
// table and the per-card price cache). A cache written at `fetchedAt` is FRESH when
// its age at `now` falls within [0, ttl): not yet expired, and not in the future.
//
// The future guard is not incidental: a clock that was ahead when the cache was
// written and later corrected backwards yields a NEGATIVE age, which a bare
// `age < ttl` would read as "fresh forever" and never re-fetch. Treating a backward
// clock as stale (re-fetch) keeps every cache consistent on this edge — the rule
// lives here precisely so the set cache and the price cache cannot drift apart on it.
inline bool cacheIsFresh(Timestamp fetchedAt, Timestamp now,
                         std::chrono::system_clock::duration ttl) {
    const std::chrono::system_clock::duration age = now - fetchedAt;
    return age >= std::chrono::system_clock::duration::zero() && age < ttl;
}

}  // namespace pokedex
