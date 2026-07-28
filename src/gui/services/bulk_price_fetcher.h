#pragma once

#include <QObject>
#include <QString>

#include <string>
#include <unordered_set>
#include <vector>

namespace pokedex {

class CardPriceLookupService;

// GUI — a bounded-concurrency bulk price refresh: re-fetch the prices of many already-linked cards
// (a binder's copies, or the whole My Cards list) on one manual "Refresh all prices" click. It
// paces the requests — at most kMaxConcurrent in flight at once, starting the next as each lands —
// so a large collection never fires a burst at the free tcgdex API. Each card's result flows
// through the normal per-card pricesReady, so the views' tables update as prices arrive.
//
// It fetches strictly by the copies' EXISTING external ids (CardPriceLookupService::fetch): it
// does NOT resolve or link unlinked copies — that is a per-copy interactive action, so the caller
// passes only linked ids. A stale legacy id simply fails its one fetch (counted as done, no harm).
// This is the lightest-impact scope; it can later grow to resolve unlinked copies via
// CardPriceFetchController if wanted.
class BulkPriceFetcher : public QObject {
    Q_OBJECT

public:
    // `lookup` must outlive this fetcher.
    explicit BulkPriceFetcher(CardPriceLookupService& lookup, QObject* parent = nullptr);

    bool isRunning() const { return total_ > 0; }

    // Begin fetching the given (assumed distinct) external card ids. A no-op while already running
    // or when the list is empty. Emits progress() as each lands and finished() once all are done.
    void start(const std::vector<std::string>& externalCardIds);

Q_SIGNALS:
    void progress(int done, int total);
    void finished();

private:
    void pump();                       // start fetches up to the concurrency limit
    void onSettled(const QString& id);  // one of our in-flight fetches finished (ok or failed)

    CardPriceLookupService& lookup_;
    std::vector<std::string> pending_;          // ids not yet started
    std::unordered_set<std::string> inFlight_;  // ids fetched, awaiting pricesReady/pricesFailed
    int total_ = 0;
    int done_ = 0;

    // At most this many concurrent GETs — the anti-burst cap. Small on purpose (a free public
    // API); bump here if bulk refreshes feel too slow.
    static constexpr int kMaxConcurrent = 4;
};

}  // namespace pokedex
