#include "core/app/card_price_service.h"

#include <utility>

#include "core/app/cache_ttl.h"
#include "core/app/card_catalog_parse.h"
#include "core/app/card_price_cache.h"
#include "core/app/uuid.h"
#include "core/strings.h"

namespace pokedex {

CardPriceService::Clock CardPriceService::systemClock() {
    return [] { return std::chrono::system_clock::now(); };
}

CardPriceService::IdGenerator CardPriceService::uuidGenerator() {
    return [] { return newUuidV4(); };
}

CardPriceService::CardPriceService(CardPriceCache& cache, Clock clock, IdGenerator idGenerator)
    : cache_(cache), clock_(std::move(clock)), idGenerator_(std::move(idGenerator)) {}

std::vector<CardPrice> CardPriceService::pricesFor(const std::string& cardKey) {
    return cache_.pricesFor(cardKey);
}

std::optional<Timestamp> CardPriceService::fetchedAt(const std::string& cardKey) {
    return cache_.fetchedAt(cardKey);
}

bool CardPriceService::needsRefresh(const std::string& cardKey, std::chrono::seconds ttl) {
    // Refresh when never fetched, expired, or the stamp is in the future (a clock
    // moved backward) — the shared cacheIsFresh rule handles the last two, including
    // the backward-clock guard the set cache uses too.
    const std::optional<Timestamp> last = cache_.fetchedAt(cardKey);
    return !last || !cacheIsFresh(*last, clock_(), ttl);
}

std::vector<CardPrice> CardPriceService::recordApiPrices(const std::string& cardKey,
                                                         const std::string& jsonPayload) {
    const Timestamp now = clock_();
    // A vendor block without a printed date falls back to the fetch time.
    std::vector<CardPrice> prices = parseCardPrices(jsonPayload, now);
    for (CardPrice& p : prices) {
        // Key every row to the id we fetched (authoritative for the cache lookup) and
        // mint its persistence id.
        p.cardKey = cardKey;
        p.id = idGenerator_();
    }
    cache_.storeApiPrices(cardKey, prices, now);
    return prices;
}

CardPrice CardPriceService::addManualPrice(const std::string& cardKey, long long amountCents,
                                           std::string currency, std::string note) {
    if (amountCents <= 0) {
        throw CardPriceError("A price must be a positive amount.");
    }
    currency = trim(currency);
    if (currency.empty()) {
        throw CardPriceError("A manual price needs a currency.");
    }

    CardPrice price;
    price.id = idGenerator_();
    price.cardKey = cardKey;
    price.provenance = kManualPriceProvenance;
    price.variant = "";
    price.metric = "";
    price.amountCents = amountCents;
    price.currency = std::move(currency);
    price.observedAt = clock_();
    price.note = trim(note);
    cache_.add(price);
    return price;
}

void CardPriceService::removeManualPrice(const std::string& id) { cache_.removeManual(id); }

}  // namespace pokedex
