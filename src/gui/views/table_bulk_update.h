#pragma once

#include <QHeaderView>
#include <QTableWidget>

#include <span>
#include <utility>
#include <vector>

namespace pokedex {

// GUI — RAII guard that makes a bulk QTableWidget rebuild cheap when the table carries
// content-sized columns. QHeaderView::ResizeToContents re-scans EVERY row's content on
// each setItem, so refilling an already-populated, visible table costs O(rows^2) and
// freezes the UI for many seconds on a large table (measured: ~16 s to replace 250×9
// cells, and effectively unbounded at ~1000 rows) — while the FIRST fill of an empty
// table is cheap because the scans grow from nothing. That asymmetry is why reopening an
// already-opened binder guide hangs but the first open does not.
//
// Constructing this guard captures each named column's current resize mode, switches it
// to Interactive (so a setItem no longer triggers a per-row rescan), and disables the
// table's repaints; restore() (also the destructor) puts each column's original mode
// back — ResizeToContents columns get a single trailing measurement pass — and re-enables
// updates. Wrap the row-fill loop:
//
//     {
//         BulkTablePopulate guard(table_, kAutoFitColumns);
//         table_->setRowCount(n);
//         ... setItem loop ...
//     }  // columns re-measured once here
//
// Pass the columns that rescan per row — the ResizeToContents ones. Because the guard
// saves and replays each column's real mode (rather than assuming ResizeToContents), a
// non-content column passed by mistake is a no-op cost, not a silent mode change. The
// rebuild drops from tens of seconds to milliseconds.
//
// restore() is idempotent: call it explicitly when the auto-fit must run before a later
// pass in the same method (e.g. a filter/selection pass that reads final column widths),
// and the destructor still fires as an exception backstop so a throw mid-populate never
// leaves the table frozen with painting off. When the whole rebuild fits one scope, just
// let the destructor do it — wrap the fill in a { } block that ends before the later work.
//
// The `contentColumns` span must outlive construction only (its values are copied into the
// saved-mode list), so a caller's static column-list array or a braced literal both work.
class BulkTablePopulate {
public:
    BulkTablePopulate(QTableWidget* table, std::span<const int> contentColumns) : table_(table) {
        auto* header = table_->horizontalHeader();
        table_->setUpdatesEnabled(false);
        saved_.reserve(contentColumns.size());
        for (const int col : contentColumns) {
            saved_.emplace_back(col, header->sectionResizeMode(col));
            header->setSectionResizeMode(col, QHeaderView::Interactive);
        }
    }

    // Replay each column's original resize mode (a ResizeToContents column re-measures
    // once here, over the finished table) and re-enable repaints. Safe to call more than
    // once — the second call is a no-op — so an explicit call and the destructor backstop
    // don't double up.
    void restore() {
        if (restored_) {
            return;
        }
        auto* header = table_->horizontalHeader();
        for (const auto& [col, mode] : saved_) {
            header->setSectionResizeMode(col, mode);
        }
        table_->setUpdatesEnabled(true);
        restored_ = true;
    }

    ~BulkTablePopulate() { restore(); }

    BulkTablePopulate(const BulkTablePopulate&) = delete;
    BulkTablePopulate& operator=(const BulkTablePopulate&) = delete;

private:
    QTableWidget* table_;
    std::vector<std::pair<int, QHeaderView::ResizeMode>> saved_;
    bool restored_ = false;
};

}  // namespace pokedex
