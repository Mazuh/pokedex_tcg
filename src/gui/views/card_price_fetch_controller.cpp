#include "gui/views/card_price_fetch_controller.h"

#include <exception>
#include <optional>

#include "core/app/card_copy_service.h"
#include "core/domain/card_copy.h"
#include "core/domain/card_ownership.h"
#include "gui/services/card_price_lookup_service.h"

namespace pokedex {

CardPriceFetchController::CardPriceFetchController(CardPriceLookupService& lookup,
                                                  CardCopyService& copies, QObject* parent)
    : QObject(parent), lookup_(lookup), copies_(copies) {
    // Our fetch landing OR another view fetching/clearing/suppressing the same card: re-read.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this, [this](const QString& id) {
        if (id == externalCardId_) {
            fetching_ = false;
            Q_EMIT pricesChanged();
        }
    });
    connect(&lookup_, &CardPriceLookupService::pricesFailed, this, [this](const QString& id) {
        // Only OUR fetch surfaces a failure: a background fetch we didn't start (or a failure for
        // another card) leaves the cached display untouched — a failed fetch changes no cache.
        if (id != externalCardId_ || !fetching_) {
            return;
        }
        fetching_ = false;
        // Neutral wording: the failure may be a busy/flaky API OR a card the provider doesn't
        // list (a 404, failed fast) — don't assert "try again" when a retry may never help.
        Q_EMIT fetchFailed(tr("Couldn't fetch prices for this card right now."));
    });
    // The lazily fetched tcgdex set table became available; a Fetch waiting on it can proceed.
    connect(&lookup_, &CardPriceLookupService::tcgdexSetsResolved, this, [this](bool ok) {
        if (!awaitingSets_) {
            return;
        }
        awaitingSets_ = false;
        if (ok) {
            resolveAndFetch();
            return;
        }
        // The set table is unavailable. If the copy already carries an id, fetch it as a best
        // effort; otherwise there is nothing to look up.
        if (!externalCardId_.isEmpty()) {
            lookup_.fetch(externalCardId_);
            return;
        }
        fetching_ = false;
        Q_EMIT fetchFailed(tr("Couldn't reach the pricing catalog. Please try again."));
    });
}

void CardPriceFetchController::setCopy(const CardCopy& copy) {
    copyId_ = copy.id;
    cardRef_ = copy.cardRef;
    externalCardId_ = QString::fromStdString(copy.externalCardId);
    copyRemoved_ = copy.ownership == CardOwnership::Removed;
    fetching_ = false;
    awaitingSets_ = false;
    triedSetRefresh_ = false;
}

void CardPriceFetchController::clearCopy() {
    copyId_.clear();
    cardRef_ = CardReference{};
    externalCardId_.clear();
    copyRemoved_ = false;
    fetching_ = false;
    awaitingSets_ = false;
    triedSetRefresh_ = false;
}

bool CardPriceFetchController::canResolve() const {
    // A soft-Removed copy is frozen history: never spend a resolve + fetch on a discarded card.
    // Otherwise a set (name or printed code) plus a collector number is all tcgdex needs.
    if (copyRemoved_ || copyId_.empty()) {
        return false;
    }
    const bool hasSet = !cardRef_.setName.empty() || !cardRef_.expansionCode.empty();
    const bool hasNumber = !cardRef_.collectorNumber.empty();
    return hasSet && hasNumber;
}

bool CardPriceFetchController::canFetch() const {
    return canResolve() || !externalCardId_.isEmpty();
}

void CardPriceFetchController::fetch() {
    if (fetching_) {
        return;  // a fetch is already in flight
    }
    if (canResolve()) {
        // Resolve the tcgdex id from set+number, then fetch. This both links an unlinked copy and
        // re-resolves one still on a pre-tcgdex id — invisibly, from either surface.
        fetching_ = true;
        triedSetRefresh_ = false;  // this explicit Fetch gets one set-refresh retry
        Q_EMIT statusMessage(tr("Looking up this card…"));
        if (lookup_.tcgdexSetsReady()) {
            resolveAndFetch();
        } else {
            awaitingSets_ = true;
            lookup_.ensureTcgdexSets();  // tcgdexSetsResolved → resolveAndFetch()
        }
        return;
    }
    if (!externalCardId_.isEmpty()) {
        // Not resolvable from what the copy records, but it already carries an id — fetch it
        // directly. An explicit user request for the latest, so hit the wire.
        fetching_ = true;
        Q_EMIT statusMessage(tr("Fetching prices…"));
        lookup_.fetch(externalCardId_);
    }
}

void CardPriceFetchController::resolveAndFetch() {
    const std::optional<QString> id = lookup_.resolveTcgdexId(cardRef_);
    if (!id) {
        // Couldn't identify the set from a (possibly cached) table. The set may be newer than our
        // cached copy, so force ONE fresh /v2/en/sets fetch and retry before giving up.
        if (!triedSetRefresh_) {
            triedSetRefresh_ = true;
            awaitingSets_ = true;
            lookup_.ensureTcgdexSets(/*forceRefresh=*/true);  // tcgdexSetsResolved → retry
            return;
        }
        // Still unidentifiable after a fresh table. Do NOT fall back to fetching externalCardId_:
        // a legitimately linked copy's tcgdex id would have re-resolved here, so a non-empty id at
        // this point is a stale/foreign key whose GET would 404 with a misleading error.
        fetching_ = false;
        Q_EMIT fetchFailed(tr("Couldn't identify this card for pricing — check its set and "
                              "collector number in “Edit card…”."));
        return;
    }

    if (*id != externalCardId_) {
        // Persist the resolved link (new, or migrated off a pre-tcgdex id) before fetching, so a
        // re-selection sees the copy as linked and the cache keys to the tcgdex id.
        try {
            copies_.linkCatalogCard(copyId_, id->toStdString());
        } catch (const std::exception&) {
            fetching_ = false;
            Q_EMIT fetchFailed(tr("Couldn't look up this card right now."));
            return;
        }
        externalCardId_ = *id;
        Q_EMIT cardLinked(QString::fromStdString(copyId_), externalCardId_);
    }
    lookup_.fetch(externalCardId_);  // pricesReady → pricesChanged()
}

}  // namespace pokedex
