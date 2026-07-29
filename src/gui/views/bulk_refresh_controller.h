#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <string>
#include <vector>

class QLabel;
class QPushButton;
class QTimer;

namespace pokedex {

class CardPriceLookupService;

// GUI — the "Refresh prices" bulk-refresh affordance for one copy-table view (the binder guide or
// My Cards), factored out of both so they can't drift. It owns the button + status-label wiring
// around CardPriceLookupService's shared bulk queue (refreshMany / bulkProgress / bulkFinished):
//
//   • click → gather the view's distinct linked ids and start the bulk (disabling the button and
//     showing "Refreshing… 0/N"); a click while a bulk is already running (this view's or another
//     view's — one service, one bulk) flashes "already running" instead of a silent no-op, and an
//     empty id set flashes "nothing linked".
//   • bulkProgress → update "Refreshing… n/m" — but ONLY in the view that started the bulk, so the
//     other view (sharing the one service) shows no phantom progress and its button stays live.
//   • bulkFinished → re-enable + hide the label in the initiating view, and ALWAYS run the view's
//     `reload` so it folds in any of its cards the bulk refreshed (the per-card pricesReady rebuild
//     is skipped for bulk-in-flight ids while the bulk runs — see the views).
//
// Transient messages (already-running / nothing-linked) auto-hide after a few seconds, guarded so
// a real progress label that replaced them is left alone.
class BulkRefreshController : public QObject {
    Q_OBJECT

public:
    // `lookup`, `button`, and `status` must outlive this controller (the view owns them).
    // `gatherIds` returns the distinct linked external ids to refresh (called per click);
    // `reload` re-reads the price cache and rebuilds the view's table.
    BulkRefreshController(CardPriceLookupService& lookup, QPushButton* button, QLabel* status,
                          std::function<std::vector<std::string>()> gatherIds,
                          std::function<void()> reload, QObject* parent = nullptr);

private:
    void start();
    void flashTransient(const QString& message);  // show + auto-hide (unless progress replaced it)

    CardPriceLookupService& lookup_;
    QPushButton* button_;
    QLabel* status_;
    std::function<std::vector<std::string>()> gatherIds_;
    std::function<void()> reload_;
    bool initiatedHere_ = false;  // this view started the running bulk → it owns the progress UI
    QTimer* transientTimer_;      // single-shot auto-hide for a transient note
    bool showingTransient_ = false;  // the label currently shows a transient (not progress)
};

}  // namespace pokedex
