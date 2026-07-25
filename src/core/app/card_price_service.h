#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/app/card_price_dto.h"
#include "core/domain/types.h"

namespace pokedex {

class CardPriceCache;

// APP — raised when a price operation is invalid, e.g. a manual price with a
// non-positive amount or a blank currency.
class CardPriceError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// APP — the verbs over a card's prices: read the cached spread, record a fresh API
// payload (parse + persist, replacing the API-sourced rows), and add/remove a
// manual price. Like CardCopyService it owns the two things storage deliberately
// does not — minting ids and reading the clock — both injectable so tests are
// deterministic.
//
// The HTTP GET itself lives GUI-side (the URL comes from
// CardCatalogApi::resolveCardById): core never does I/O, so the caller fetches the
// per-card payload and hands its JSON to recordApiPrices. A card's prices are keyed
// by the external card id and are independent of any owned CardCopy.
class CardPriceService {
public:
    using Clock = std::function<Timestamp()>;
    using IdGenerator = std::function<std::string()>;

    explicit CardPriceService(CardPriceCache& cache, Clock clock = systemClock(),
                              IdGenerator idGenerator = uuidGenerator());

    // Every stored price for a card (API-sourced + manual), the cached spread the
    // caller displays. Reads only — never hits the network.
    std::vector<CardPrice> pricesFor(const std::string& externalCardId);

    // When we last fetched this card from the API, or nullopt if never.
    std::optional<Timestamp> fetchedAt(const std::string& externalCardId);

    // Whether a fresh API fetch is warranted: true when the card was never fetched
    // or its last fetch is at least `ttl` old. The anti-hammer gate the caller
    // checks before doing the (rate-limited, flaky) network GET.
    bool needsRefresh(const std::string& externalCardId, std::chrono::seconds ttl);

    // Parse a /v2/cards/{id} payload and persist its prices for `externalCardId`, replacing
    // that card's previously-cached API rows and stamping the fetch time to now();
    // manual rows are preserved. The parsed rows are re-keyed to `externalCardId` (the id we
    // fetched) so a read finds them regardless of the payload's own id field. Returns
    // the freshly stored API prices. Throws CardCatalogParseError if the payload is
    // not valid JSON (a valid-but-empty payload simply stores no prices).
    std::vector<CardPrice> recordApiPrices(const std::string& externalCardId,
                                           const std::string& jsonPayload);

    // Pin a manual price for a card. `amountCents` must be positive and `currency`
    // non-blank (trimmed) — else CardPriceError. observedAt is stamped to now().
    // Returns the persisted price with its minted id.
    CardPrice addManualPrice(const std::string& externalCardId, long long amountCents,
                             std::string currency, std::string note = "");

    // Delete a manual price by id. A no-op if the id is unknown or names an
    // API-sourced row (those are managed only by recordApiPrices).
    void removeManualPrice(const std::string& id);

    // The defaults, exposed so callers can wrap/compose them if needed.
    static Clock systemClock();
    static IdGenerator uuidGenerator();

private:
    CardPriceCache& cache_;
    Clock clock_;
    IdGenerator idGenerator_;
};

}  // namespace pokedex
