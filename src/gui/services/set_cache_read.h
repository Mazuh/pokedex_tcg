#pragma once

#include <chrono>
#include <optional>
#include <vector>

#include "core/app/cache_ttl.h"
#include "core/app/card_catalog_dto.h"
#include "core/app/card_set_cache.h"
#include "core/domain/types.h"

namespace pokedex {

// GUI — the ONE freshness rule + TTL both set-table disk caches share (the catalog set list
// behind CardSearchService and the tcgdex set list behind CardPriceLookupService). Extracted
// so the two can't drift on when a cached table is reused vs. re-fetched: tune the TTL or the
// stale-fallback rule here and both services follow. Each service still owns what it does with
// the result (its own members / signals / logging).

// Skip the /sets fetch when the on-disk cache is younger than this.
inline constexpr auto kSetCacheTtl = std::chrono::hours(24);

// The cached sets for `cache`, or nullopt when there is nothing usable: never fetched, too old
// (when `requireFresh` — the no-network fast path; false is the post-failure stale fallback,
// which accepts any age), or an empty table (no better than no table). May throw on a DB read
// error — call within the caller's try/catch so a corrupt/locked DB degrades to "no cache"
// rather than crashing.
inline std::optional<std::vector<CardSetInfo>> readSetCache(CardSetCache& cache,
                                                            bool requireFresh) {
    const std::optional<Timestamp> fetchedAt = cache.fetchedAt();
    if (!fetchedAt) {
        return std::nullopt;  // never fetched
    }
    // cacheIsFresh also rejects a future-dated stamp (a backward clock) — see cache_ttl.h.
    if (requireFresh && !cacheIsFresh(*fetchedAt, std::chrono::system_clock::now(), kSetCacheTtl)) {
        return std::nullopt;  // stale (or future) — the caller will re-fetch
    }
    std::vector<CardSetInfo> sets = cache.load();
    if (sets.empty()) {
        return std::nullopt;
    }
    return sets;
}

}  // namespace pokedex
