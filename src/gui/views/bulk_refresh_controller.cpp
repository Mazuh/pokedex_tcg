#include "gui/views/bulk_refresh_controller.h"

#include <QLabel>
#include <QPushButton>
#include <QTimer>

#include <utility>

#include "gui/services/card_price_lookup_service.h"

namespace pokedex {

namespace {
constexpr int kTransientHideMs = 5000;  // how long an already-running / nothing-linked note shows
}  // namespace

BulkRefreshController::BulkRefreshController(CardPriceLookupService& lookup, QPushButton* button,
                                             QLabel* status,
                                             std::function<std::vector<std::string>()> gatherIds,
                                             std::function<void()> reload, QObject* parent)
    : QObject(parent),
      lookup_(lookup),
      button_(button),
      status_(status),
      gatherIds_(std::move(gatherIds)),
      reload_(std::move(reload)) {
    // Auto-hide a transient note (already-running / nothing-linked). One restarting single-shot
    // timer, hiding only while the label still shows a transient — so progress that replaced it,
    // or a second transient that restarted the timer, is left to manage itself.
    transientTimer_ = new QTimer(this);
    transientTimer_->setSingleShot(true);
    connect(transientTimer_, &QTimer::timeout, this, [this]() {
        if (showingTransient_) {
            status_->hide();
            showingTransient_ = false;
        }
    });
    connect(button_, &QPushButton::clicked, this, &BulkRefreshController::start);
    connect(&lookup_, &CardPriceLookupService::bulkProgress, this, [this](int done, int total) {
        if (initiatedHere_) {  // the other view (one shared service) shows no phantom progress
            showingTransient_ = false;  // progress owns the label now
            status_->setText(tr("Refreshing… %1/%2").arg(done).arg(total));
            status_->show();
        }
    });
    connect(&lookup_, &CardPriceLookupService::bulkFinished, this, [this]() {
        if (initiatedHere_) {
            status_->hide();
            button_->setEnabled(true);
            initiatedHere_ = false;
        }
        // One full rebuild now the bulk is done: the per-card pricesReady already rewrote each
        // affected Prices cell live (updatePricesFor, no repopulate), so this exists only to
        // re-sort (a Prices-column sort may have gone stale as figures changed) and settle the
        // final state — one rebuild, not one per arriving price.
        reload_();
    });
}

void BulkRefreshController::start() {
    if (lookup_.bulkRunning()) {
        // One service, one bulk at a time — a click here (or after starting one elsewhere) while
        // any bulk runs is refused with feedback rather than a silent no-op.
        flashTransient(tr("A price refresh is already running."));
        return;
    }
    const std::vector<std::string> ids = gatherIds_();
    if (ids.empty()) {
        flashTransient(tr("No linked cards to refresh — fetch a card's prices first."));
        return;
    }
    initiatedHere_ = true;
    showingTransient_ = false;  // progress owns the label now
    button_->setEnabled(false);
    status_->setText(tr("Refreshing… 0/%1").arg(ids.size()));
    status_->show();
    lookup_.refreshMany(ids);
}

void BulkRefreshController::flashTransient(const QString& message) {
    showingTransient_ = true;
    status_->setText(message);
    status_->show();
    transientTimer_->start(kTransientHideMs);  // restart: a later note extends the window
}

}  // namespace pokedex
