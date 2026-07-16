#pragma once

#include <cstdint>
#include <optional>

namespace pokedex {

// APP — a monotonic token-bucket rate limiter: a small, Qt-free, clock-injected
// value type used as a hard backstop on the rate of outbound work (today, the
// GUI's external-API media GETs; reusable verbatim for any future external
// call). It answers one question — "may I do the next request right now?" — and
// deliberately does nothing else: it never sleeps, throws, logs, or reads the
// clock. The caller supplies the current time and decides what a denial means
// (drop it, retry later, surface a message).
//
// Model. The bucket holds up to `capacity` tokens and refills at
// `refillPerSecond`. Each grant costs one token. Starting full means a burst of
// up to `capacity` requests sails through instantly from idle; once drained, the
// steady-state grant rate is exactly `refillPerSecond`. Choosing capacity vs.
// refill separates "how bursty may it be" from "what sustained rate is safe".
//
// Clock discipline (mirrors the app-layer convention that the domain never reads
// the clock): `tryAcquire` takes a caller-supplied monotonic millisecond
// timestamp — typically std::chrono::steady_clock. Passing time in keeps the
// limiter fully deterministic and unit-testable with fixed timestamps, and lets
// one clock source drive many buckets. A non-advancing or backwards timestamp
// refills nothing (a defensive guard against a non-monotonic source); it never
// corrupts the accumulated state.
class TokenBucket {
public:
    // `capacity` is the largest burst allowed from idle (tokens the bucket can
    // hold); `refillPerSecond` is the sustained grant rate once that burst is
    // spent. Both should be >= 0. The bucket starts full, so `capacity` grants
    // are available immediately. A `capacity` below 1 can never hold a whole
    // token and so never grants; a `refillPerSecond` of 0 is a legitimate
    // burst-only bucket that never replenishes.
    TokenBucket(double capacity, double refillPerSecond);

    // Refill by the time elapsed since the previous observation (capped at
    // capacity), then consume and grant one token iff a whole token is
    // available. Returns true and decrements on grant; returns false and leaves
    // the bucket untouched when the budget is spent. `nowMs` is a monotonic
    // millisecond timestamp supplied by the caller.
    bool tryAcquire(std::int64_t nowMs);

private:
    // Advance the token count to reflect elapsed time, capped at capacity. The
    // first observation only anchors the reference time (the bucket is already
    // full); a non-advancing/backwards `nowMs` is ignored so it neither adds
    // tokens nor rewinds the reference.
    void refill(std::int64_t nowMs);

    double capacity_;
    double refillPerMs_;
    double tokens_;
    std::optional<std::int64_t> lastMs_;
};

}  // namespace pokedex
