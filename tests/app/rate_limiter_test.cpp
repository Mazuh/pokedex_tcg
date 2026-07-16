#include "core/app/rate_limiter.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using pokedex::TokenBucket;

// A full bucket lets a burst of exactly `capacity` grants through at one instant,
// then denies — the burst allowance is the capacity, no more.
TEST(TokenBucketTest, StartsFullAndAllowsBurstUpToCapacity) {
    TokenBucket bucket(3.0, 1.0);
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_FALSE(bucket.tryAcquire(0));  // 4th at the same instant: spent
}

// Repeated calls at the same timestamp never refill: once drained it stays
// denied until time actually advances.
TEST(TokenBucketTest, EqualTimestampsNeverRefill) {
    TokenBucket bucket(2.0, 100.0);  // fast refill, but time is frozen
    EXPECT_TRUE(bucket.tryAcquire(500));
    EXPECT_TRUE(bucket.tryAcquire(500));
    EXPECT_FALSE(bucket.tryAcquire(500));
    EXPECT_FALSE(bucket.tryAcquire(500));
}

// After draining, a grant becomes available only once enough time has elapsed to
// replenish a whole token — and not a millisecond before.
TEST(TokenBucketTest, RefillsExactlyOneTokenAtTheRefillInterval) {
    TokenBucket bucket(1.0, 5.0);  // 5/s => one token per 200ms
    EXPECT_TRUE(bucket.tryAcquire(0));       // drains the initial token
    EXPECT_FALSE(bucket.tryAcquire(199));    // 199ms => 0.995 tokens, still denied
    EXPECT_TRUE(bucket.tryAcquire(200));     // 200ms => a whole token, granted
    EXPECT_FALSE(bucket.tryAcquire(200));    // and immediately spent again
}

// Refill is proportional to elapsed time: waiting for N intervals yields N
// tokens (up to capacity).
TEST(TokenBucketTest, RefillIsProportionalToElapsedTime) {
    TokenBucket bucket(10.0, 5.0);  // one token per 200ms
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(bucket.tryAcquire(0));  // drain all 10
    }
    EXPECT_FALSE(bucket.tryAcquire(0));
    // 600ms => 3 tokens replenished.
    EXPECT_TRUE(bucket.tryAcquire(600));
    EXPECT_TRUE(bucket.tryAcquire(600));
    EXPECT_TRUE(bucket.tryAcquire(600));
    EXPECT_FALSE(bucket.tryAcquire(600));
}

// Idling longer than it takes to fill the bucket never accumulates beyond
// capacity — the burst allowance is bounded no matter how long you wait.
TEST(TokenBucketTest, RefillCapsAtCapacity) {
    TokenBucket bucket(2.0, 100.0);  // would refill 1000 tokens in 10s
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_FALSE(bucket.tryAcquire(0));
    // Wait 10 whole seconds; capacity is still only 2.
    EXPECT_TRUE(bucket.tryAcquire(10'000));
    EXPECT_TRUE(bucket.tryAcquire(10'000));
    EXPECT_FALSE(bucket.tryAcquire(10'000));
}

// Fractional tokens accumulate across calls: two half-intervals add up to one
// whole token, granted on the call that crosses the threshold.
TEST(TokenBucketTest, FractionalTokensAccumulateAcrossCalls) {
    TokenBucket bucket(5.0, 1.0);  // one token per 1000ms
    for (int i = 0; i < 5; ++i) {
        EXPECT_TRUE(bucket.tryAcquire(0));  // drain
    }
    EXPECT_FALSE(bucket.tryAcquire(0));
    EXPECT_FALSE(bucket.tryAcquire(500));   // +0.5 token => 0.5, denied
    EXPECT_TRUE(bucket.tryAcquire(1000));   // +0.5 more => 1.0, granted
    EXPECT_FALSE(bucket.tryAcquire(1000));  // back to 0
}

// A backwards (non-monotonic) timestamp must not mint tokens, and must not
// corrupt the reference: forward progress measured from the latest time still
// refills correctly afterwards.
TEST(TokenBucketTest, NonMonotonicTimeRefillsNothingAndDoesNotCorruptState) {
    TokenBucket bucket(1.0, 5.0);  // one token per 200ms
    EXPECT_TRUE(bucket.tryAcquire(1000));  // drain; reference at 1000
    EXPECT_FALSE(bucket.tryAcquire(500));  // time jumped backwards: no refill
    EXPECT_FALSE(bucket.tryAcquire(900));  // still behind the 1000 reference
    EXPECT_FALSE(bucket.tryAcquire(1100));  // only 100ms past reference => 0.5
    EXPECT_TRUE(bucket.tryAcquire(1200));  // 200ms past reference => a whole token
}

// The first observation only anchors the clock; it must not credit tokens for
// all the time since the epoch, which would let an unbounded burst through.
TEST(TokenBucketTest, FirstCallAnchorsTimeWithoutOverfilling) {
    TokenBucket bucket(3.0, 1000.0);  // huge refill rate
    const std::int64_t farFuture = 1'000'000'000;
    EXPECT_TRUE(bucket.tryAcquire(farFuture));
    EXPECT_TRUE(bucket.tryAcquire(farFuture));
    EXPECT_TRUE(bucket.tryAcquire(farFuture));
    EXPECT_FALSE(bucket.tryAcquire(farFuture));  // only capacity, not billions
}

// A zero refill rate is a legitimate burst-only limiter: `capacity` grants total,
// then never again regardless of how much time passes.
TEST(TokenBucketTest, ZeroRefillIsBurstOnly) {
    TokenBucket bucket(2.0, 0.0);
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_FALSE(bucket.tryAcquire(0));
    EXPECT_FALSE(bucket.tryAcquire(1'000'000));  // no refill, ever
}

// A capacity below one whole token can never hold enough to grant.
TEST(TokenBucketTest, SubUnitCapacityNeverGrants) {
    TokenBucket bucket(0.0, 10.0);
    EXPECT_FALSE(bucket.tryAcquire(0));
    EXPECT_FALSE(bucket.tryAcquire(1000));  // refill is capped at capacity (0)
}

// Capacity and refill are independent knobs: a small capacity bounds the burst
// while the refill sets the sustained rate. Here capacity 1 forces strict
// spacing at the refill interval with no burst.
TEST(TokenBucketTest, UnitCapacityEnforcesStrictSpacing) {
    TokenBucket bucket(1.0, 2.0);  // one token per 500ms, no burst
    EXPECT_TRUE(bucket.tryAcquire(0));
    EXPECT_FALSE(bucket.tryAcquire(499));
    EXPECT_TRUE(bucket.tryAcquire(500));
    EXPECT_FALSE(bucket.tryAcquire(999));
    EXPECT_TRUE(bucket.tryAcquire(1000));
}

// End-to-end pacing: hammering the bucket far faster than it refills yields
// grants bounded by (initial burst + refill * duration), not by the call count.
// This is the property that protects the external API from a key-repeat storm.
TEST(TokenBucketTest, SustainedHammeringIsPacedToTheRefillRate) {
    TokenBucket bucket(5.0, 5.0);  // burst 5, then 5/s
    int grants = 0;
    // Call every 20ms for 2000ms => 101 attempts, far above the allowed rate.
    for (std::int64_t t = 0; t <= 2000; t += 20) {
        if (bucket.tryAcquire(t)) {
            ++grants;
        }
    }
    // Budget over the 2s window: 5 (initial) + 5/s * 2s = 15. Allow ±1 for the
    // discrete sampling boundary.
    EXPECT_GE(grants, 14);
    EXPECT_LE(grants, 16);
}

}  // namespace
