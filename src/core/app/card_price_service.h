#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
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
// The HTTP GET itself lives GUI-side (CardPriceLookupService fetches the tcgdex
// per-card payload): core never does I/O, so the caller hands the payload's JSON to
// recordTcgdexPrices. A card's prices are keyed by the external card id and are
// independent of any owned CardCopy.
class CardPriceService {
public:
    using Clock = std::function<Timestamp()>;
    using IdGenerator = std::function<std::string()>;

    explicit CardPriceService(CardPriceCache& cache, Clock clock = systemClock(),
                              IdGenerator idGenerator = uuidGenerator());

    // Every stored price for a card (API-sourced + manual), the cached spread the
    // caller displays. Reads only — never hits the network.
    std::vector<CardPrice> pricesFor(const std::string& externalCardId);

    // The cached prices for many cards at once, keyed by external card id — one batched
    // query rather than N pricesFor calls, for a caller totalling a whole collection's
    // value. Reads only.
    std::unordered_map<std::string, std::vector<CardPrice>> pricesForMany(
        const std::vector<std::string>& externalCardIds);

    // When we last fetched this card from the API, or nullopt if never.
    std::optional<Timestamp> fetchedAt(const std::string& externalCardId);

    // The outcome of recordTcgdexPrices. `stored` are the freshly persisted API rows (empty
    // when the card came back with no vendor blocks). `degraded` is true when NO card
    // object came back at all (an error body / a 200 the transport couldn't turn into a
    // card): nothing was stored or stamped, so the caller must treat the fetch as FAILED
    // (let the user retry) rather than as a successful "no prices" answer — a genuinely
    // price-less card (card present, no blocks) has degraded=false and an empty `stored`,
    // and IS stamped so it isn't re-offered forever. See CardPricesParse.
    struct RecordedApiPrices {
        std::vector<CardPrice> stored;
        bool degraded = false;
    };

    // Parse a tcgdex /v2/en/cards/{id} payload (parseTcgdexCardPrices) and persist its prices
    // for `externalCardId` — the app's sole automated pricing PROVIDER. Replaces that card's
    // API-sourced rows and stamps the fetch time to now(); manual rows are preserved. The
    // parsed rows are re-keyed to `externalCardId` (the id we fetched) so a read finds them
    // regardless of the payload's own id field. Returns the freshly stored API prices, plus a
    // `degraded` flag distinguishing a no-card response (nothing stored/stamped) from a genuine
    // price-less answer. Throws CardCatalogParseError if the payload is not valid JSON.
    RecordedApiPrices recordTcgdexPrices(const std::string& externalCardId,
                                         const std::string& jsonPayload);

    // Pin a manual price for a card. `amountCents` must be positive and `currency`
    // non-blank (trimmed) — else CardPriceError. observedAt is stamped to now().
    // Returns the persisted price with its minted id.
    CardPrice addManualPrice(const std::string& externalCardId, long long amountCents,
                             std::string currency, std::string note = "");

    // Delete a manual price by id. A no-op if the id is unknown or names an
    // API-sourced row (those are managed only by recordTcgdexPrices).
    void removeManualPrice(const std::string& id);

    // Forget all cached prices (fetched AND manual) and the fetch stamp for a card, returning
    // it to the never-fetched state. A later fetch re-populates it. A no-op when nothing is
    // cached. (Prices are keyed by the card, not the copy, so this affects every copy that
    // shares the id — the same as a fetch.) Also drops any vendor suppressions, so Clear is
    // the one "ground zero" reset.
    void clearPrices(const std::string& externalCardId);

    // Hide / un-hide a vendor's price for a card (a per-card, per-vendor suppression). A
    // suppression survives a Refresh (recordTcgdexPrices never touches it) and is dropped only
    // by clearPrices — the design that lets the user permanently remove a vendor whose tcgdex
    // mapping is wrong for their card, while still refreshing the vendors they keep.
    void suppressVendor(const std::string& externalCardId, const std::string& provenance);
    void unsuppressVendor(const std::string& externalCardId, const std::string& provenance);

    // The vendors currently suppressed for a card, and the same batched for many cards (for
    // the card tables). Reads only. Cards with no suppression are absent from the map.
    std::vector<std::string> suppressedVendors(const std::string& externalCardId);
    std::unordered_map<std::string, std::vector<std::string>> suppressedVendorsForMany(
        const std::vector<std::string>& externalCardIds);

    // The defaults, exposed so callers can wrap/compose them if needed.
    static Clock systemClock();
    static IdGenerator uuidGenerator();

private:
    // Shared persistence path behind recordTcgdexPrices: key + mint ids onto the parsed rows
    // and store them (stamping `now`), or, for a degraded response (no card, so `prices` empty
    // and `cardPresent` false), leave the cache and stamp untouched and report it. The degraded
    // case must NOT cache a false "no prices for this card": a transport/parse failure is not
    // evidence the card is unpriced, and stamping it would make a later Refresh look up-to-date
    // and hide the real prices behind a bogus empty cache.
    RecordedApiPrices persistParsedPrices(const std::string& externalCardId,
                                          std::vector<CardPrice> prices, bool cardPresent,
                                          Timestamp now);

    CardPriceCache& cache_;
    Clock clock_;
    IdGenerator idGenerator_;
};

}  // namespace pokedex
