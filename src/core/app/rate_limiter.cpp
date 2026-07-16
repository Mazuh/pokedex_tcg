#include "core/app/rate_limiter.h"

#include <algorithm>

namespace pokedex {

namespace {
// Guard the whole-token comparison against floating-point drift: an elapsed
// span that should replenish exactly one token (e.g. 200ms at 5/s) can land a
// hair under 1.0 in binary, which would otherwise starve a grant that is due.
constexpr double kEpsilon = 1e-9;
}  // namespace

TokenBucket::TokenBucket(double capacity, double refillPerSecond)
    : capacity_(capacity),
      refillPerMs_(refillPerSecond / 1000.0),
      tokens_(capacity) {}

void TokenBucket::refill(std::int64_t nowMs) {
    if (!lastMs_) {
        // First observation: the bucket starts full, so only anchor the
        // reference time — do NOT credit elapsed-since-epoch tokens.
        lastMs_ = nowMs;
        return;
    }
    const std::int64_t elapsed = nowMs - *lastMs_;
    if (elapsed <= 0) {
        // No forward progress (equal timestamp) or a backwards, non-monotonic
        // blip: refill nothing and leave the reference at the latest time seen.
        return;
    }
    tokens_ = std::min(capacity_, tokens_ + static_cast<double>(elapsed) * refillPerMs_);
    lastMs_ = nowMs;
}

bool TokenBucket::tryAcquire(std::int64_t nowMs) {
    refill(nowMs);
    if (tokens_ + kEpsilon >= 1.0) {
        tokens_ -= 1.0;
        return true;
    }
    return false;
}

}  // namespace pokedex
