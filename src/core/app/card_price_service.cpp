#include "core/app/card_price_service.h"

#include <algorithm>
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

bool CardPriceService::needsRefresh(const std::string& externalCardId, std::chrono::seconds ttl) {
    // Refresh when never fetched, expired, or the stamp is in the future (a clock
    // moved backward) — the shared cacheIsFresh rule handles the last two, including
    // the backward-clock guard the set cache uses too.
    const std::optional<Timestamp> last = cache_.fetchedAt(externalCardId);
    return !last || !cacheIsFresh(*last, clock_(), ttl);
}

std::vector<CardPrice> CardPriceService::recordApiPrices(const std::string& externalCardId,
                                                         const std::string& jsonPayload) {
    const Timestamp now = clock_();
    // A vendor block without a printed date falls back to the fetch time.
    std::vector<CardPrice> prices = parseCardPrices(jsonPayload, now);

    // A degraded response — the card object came back with no price blocks — must not
    // wipe prices we already hold (mirrors the set cache's empty-200 guard). Only when
    // there are no API-sourced rows yet do we store the empty result, which stamps the
    // fetch so a genuinely price-less card reads "no prices" instead of re-offering
    // Fetch forever. Manual rows are irrelevant to this check (storeApiPrices keeps them).
    if (prices.empty()) {
        const std::vector<CardPrice> existing = cache_.pricesFor(externalCardId);
        const bool hasApiRows =
            std::any_of(existing.begin(), existing.end(),
                        [](const CardPrice& p) { return p.provenance != kManualPriceProvenance; });
        if (hasApiRows) {
            return {};  // keep the good cached prices; leave the fetch stamp untouched
        }
    }

    for (CardPrice& p : prices) {
        // Key every row to the id we fetched (authoritative for the cache lookup) and
        // mint its persistence id.
        p.externalCardId = externalCardId;
        p.id = idGenerator_();
    }
    cache_.storeApiPrices(externalCardId, prices, now);
    return prices;
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

}  // namespace pokedex
