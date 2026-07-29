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
    connect(button_, &QPushButton::clicked, this, &BulkRefreshController::start);
    connect(&lookup_, &CardPriceLookupService::bulkProgress, this, [this](int done, int total) {
        if (initiatedHere_) {  // the other view (one shared service) shows no phantom progress
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
        // Always fold the finished bulk into this view: its per-card pricesReady rebuild was
        // skipped for bulk-in-flight ids while the bulk ran, so any of this view's cards the bulk
        // refreshed are only reflected here now (one rebuild, not one per arriving price).
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
    button_->setEnabled(false);
    status_->setText(tr("Refreshing… 0/%1").arg(ids.size()));
    status_->show();
    lookup_.refreshMany(ids);
}

void BulkRefreshController::flashTransient(const QString& message) {
    status_->setText(message);
    status_->show();
    // Auto-hide, but only if a real progress label (or another message) hasn't replaced it since —
    // so a bulk that starts within the window keeps its "Refreshing…" line.
    QTimer::singleShot(kTransientHideMs, this, [this, message]() {
        if (status_->text() == message) {
            status_->hide();
        }
    });
}

}  // namespace pokedex
