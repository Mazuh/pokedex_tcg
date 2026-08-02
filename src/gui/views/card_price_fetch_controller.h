#pragma once

#include <QObject>
#include <QString>

#include <string>

#include "core/domain/card_reference.h"

namespace pokedex {

class CardPriceLookupService;
class CardCopyService;
struct CardCopy;

// GUI — the shared fetch/resolve/link orchestration for one owned copy's prices, used by BOTH the
// read-only CardPricesSummary (its inline Fetch/Refresh) and the interactive CardPricesPanel (on
// the management page). Extracted so the two surfaces share ONE fetch path instead of the summary
// re-implementing — and drifting from — the panel's state machine. In particular both get the
// invisible re-resolution that self-heals a copy still linked to a pre-tcgdex id: a Fetch resolves
// the tcgdex card id from the copy's set + collector number, links the copy if the id changed
// (persisting via CardCopyService), then fetches. A Fetch is therefore never a dead-end on a
// legacy id, from either surface.
//
// It is non-visual: it owns the fetch state (copy context, the in-flight flag, the one set-table
// refresh retry) and drives the network via CardPriceLookupService, reporting progress through
// signals the widgets render. It is the single subscriber to the lookup service's per-card
// signals; widgets talk only to the controller.
class CardPriceFetchController : public QObject {
    Q_OBJECT

public:
    // `lookup` and `copies` must outlive this controller.
    CardPriceFetchController(CardPriceLookupService& lookup, CardCopyService& copies,
                             QObject* parent = nullptr);

    // Point at a copy (its id / printed reference / tcgdex link / removed-state); cancels any
    // in-flight fetch bookkeeping. Does not itself fetch.
    void setCopy(const CardCopy& copy);
    // Empty context: no copy.
    void clearCopy();

    // Whether the copy can be resolved to a tcgdex id from its set + collector number — the gate
    // for a first/re-resolving Fetch on an unlinked or legacy-id copy. False for a soft-Removed
    // copy (frozen history) or one lacking a set/number.
    bool canResolve() const;
    // Whether a Fetch is possible at all: resolvable, or already carrying an id to re-fetch.
    bool canFetch() const;
    bool isFetching() const { return fetching_; }
    // The copy's current tcgdex link (updated in place when a Fetch re-resolves it); empty when
    // unlinked.
    QString externalCardId() const { return externalCardId_; }

public Q_SLOTS:
    // Resolve (when possible) → link if the id changed → fetch. A no-op while a fetch is in flight
    // or when nothing can be fetched. Emits statusMessage as it proceeds and, on completion,
    // pricesChanged (success) or fetchFailed (terminal failure). This is the MANUAL path (a
    // Fetch/Refresh button): it always hits the wire, ignoring the price TTL — an explicit
    // "get the latest".
    void fetch();

    // The AUTOMATIC path (a price fetch kicked off when a copy is added, not by a button): same
    // resolve → link, but it skips the network when the card's cached prices are still fresh
    // (lookup.pricesFresh) rather than re-hitting a free API for a card just priced — the common
    // case of adding several copies of one card from a booster. The link is still resolved and
    // persisted (so the copy is priced from cache), and pricesChanged is emitted either way.
    void autoFetch();

Q_SIGNALS:
    // Transient progress for the status line ("Looking up this card…" / "Fetching prices…").
    void statusMessage(const QString& text);
    // This card's cached prices changed — our fetch completed, OR another view fetched/cleared/
    // suppressed the same card. The widget re-reads the cache and re-renders.
    void pricesChanged();
    // OUR fetch failed terminally (a background fetch we didn't start never fires this).
    void fetchFailed(const QString& message);
    // A Fetch resolved and persisted this copy's tcgdex link (new, or migrated off a legacy id).
    void cardLinked(const QString& copyId, const QString& externalCardId);

private:
    void resolveAndFetch();
    // Issue the actual price fetch (the tail of both fetch() and resolveAndFetch()). In auto mode
    // this skips the wire when the cached prices are still fresh, reporting the cache instead.
    void issueFetch();

    CardPriceLookupService& lookup_;
    CardCopyService& copies_;

    std::string copyId_;
    CardReference cardRef_;
    QString externalCardId_;         // empty == not yet linked
    bool copyRemoved_ = false;       // a soft-Removed copy — never resolve/fetch
    bool fetching_ = false;          // a Fetch (resolve and/or price fetch) is in flight
    bool autoMode_ = false;          // this Fetch was the automatic on-add path (honours the TTL)
    bool awaitingSets_ = false;      // waiting on the tcgdex set table before resolveAndFetch()
    bool triedSetRefresh_ = false;   // already forced one set-table refresh this Fetch — don't loop
};

}  // namespace pokedex
