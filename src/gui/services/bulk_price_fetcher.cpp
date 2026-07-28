#include "gui/services/bulk_price_fetcher.h"

#include "gui/services/card_price_lookup_service.h"

namespace pokedex {

BulkPriceFetcher::BulkPriceFetcher(CardPriceLookupService& lookup, QObject* parent)
    : QObject(parent), lookup_(lookup) {
    // fetch() always emits exactly one of these per id (terminal), so each tracked id settles
    // exactly once. A signal for an id we aren't tracking (another view's fetch) is ignored.
    connect(&lookup_, &CardPriceLookupService::pricesReady, this,
            [this](const QString& id) { onSettled(id); });
    connect(&lookup_, &CardPriceLookupService::pricesFailed, this,
            [this](const QString& id) { onSettled(id); });
}

void BulkPriceFetcher::start(const std::vector<std::string>& externalCardIds) {
    if (isRunning() || externalCardIds.empty()) {
        return;
    }
    pending_.assign(externalCardIds.begin(), externalCardIds.end());
    inFlight_.clear();
    total_ = static_cast<int>(pending_.size());
    done_ = 0;
    Q_EMIT progress(done_, total_);
    pump();
}

void BulkPriceFetcher::pump() {
    while (static_cast<int>(inFlight_.size()) < kMaxConcurrent && !pending_.empty()) {
        const std::string id = pending_.back();
        pending_.pop_back();
        inFlight_.insert(id);
        lookup_.fetch(QString::fromStdString(id));
    }
}

void BulkPriceFetcher::onSettled(const QString& id) {
    const auto it = inFlight_.find(id.toStdString());
    if (it == inFlight_.end()) {
        return;  // not one of ours (another view's fetch, or a duplicate signal)
    }
    inFlight_.erase(it);
    ++done_;
    Q_EMIT progress(done_, total_);
    if (pending_.empty() && inFlight_.empty()) {
        total_ = 0;  // back to not-running (isRunning())
        done_ = 0;
        Q_EMIT finished();
        return;
    }
    pump();  // top the in-flight set back up
}

}  // namespace pokedex
