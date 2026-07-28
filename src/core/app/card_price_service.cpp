#include "core/app/card_price_service.h"

#include <chrono>
#include <utility>

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

std::vector<CardPrice> CardPriceService::pricesFor(const std::string& externalCardId) {
    return cache_.pricesFor(externalCardId);
}

std::unordered_map<std::string, std::vector<CardPrice>> CardPriceService::pricesForMany(
    const std::vector<std::string>& externalCardIds) {
    return cache_.pricesForMany(externalCardIds);
}

std::optional<Timestamp> CardPriceService::fetchedAt(const std::string& externalCardId) {
    return cache_.fetchedAt(externalCardId);
}

CardPriceService::RecordedApiPrices CardPriceService::recordTcgdexPrices(
    const std::string& externalCardId, const std::string& jsonPayload) {
    const Timestamp now = clock_();
    const CardPricesParse parsed = parseTcgdexCardPricesResult(jsonPayload, now);
    return persistParsedPrices(externalCardId, parsed.prices, parsed.cardPresent, now);
}

CardPriceService::RecordedApiPrices CardPriceService::persistParsedPrices(
    const std::string& externalCardId, std::vector<CardPrice> prices, bool cardPresent,
    Timestamp now) {
    // A *degraded* response — no card object came back at all (an error body / data:null /
    // a failure the transport surfaced as a 200) — is NOT a real "no prices" answer: never
    // store or stamp it, whether or not we already hold prices. Any cached prices stay
    // intact, and a first-ever fetch stays unfetched (no stamp), so the next Fetch/Refresh
    // retries rather than the card showing a false "No market prices" verdict. A card that
    // DID come back (present) with no price blocks — a delisted card, or a set the provider
    // hasn't priced yet — is a genuine "no prices" answer and falls through below to clear +
    // stamp, caching the blank so it isn't re-fetched until the user insists via Refresh.
    // Manual rows are untouched either way (storeApiPrices keeps them).
    if (prices.empty() && !cardPresent) {
        // Degraded: leave the cache and the (absent-or-old) stamp untouched, and tell the
        // caller so it surfaces a retryable failure rather than a false "no prices".
        return {.stored = {}, .degraded = true};
    }

    for (CardPrice& p : prices) {
        // Key every row to the id we fetched (authoritative for the cache lookup) and
        // mint its persistence id.
        p.externalCardId = externalCardId;
        p.id = idGenerator_();
    }
    cache_.storeApiPrices(externalCardId, prices, now);
    return {.stored = std::move(prices), .degraded = false};
}

CardPrice CardPriceService::addManualPrice(const std::string& externalCardId, long long amountCents,
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
    price.externalCardId = externalCardId;
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

void CardPriceService::clearPrices(const std::string& externalCardId) {
    cache_.clear(externalCardId);
}

void CardPriceService::suppressVendor(const std::string& externalCardId,
                                      const std::string& provenance) {
    cache_.suppressVendor(externalCardId, provenance);
}

void CardPriceService::unsuppressVendor(const std::string& externalCardId,
                                        const std::string& provenance) {
    cache_.unsuppressVendor(externalCardId, provenance);
}

std::vector<std::string> CardPriceService::suppressedVendors(const std::string& externalCardId) {
    return cache_.suppressedVendors(externalCardId);
}

std::unordered_map<std::string, std::vector<std::string>>
CardPriceService::suppressedVendorsForMany(const std::vector<std::string>& externalCardIds) {
    return cache_.suppressedVendorsForMany(externalCardIds);
}

}  // namespace pokedex
