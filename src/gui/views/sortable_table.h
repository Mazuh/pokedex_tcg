#pragma once

#include <QHeaderView>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTableWidget>

#include "gui/views/tooltip_text.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

namespace pokedex {

// GUI — make a table's column headers sort the view by that field on click.
//
// This is the single place the "click a header to sort" behavior lives; every
// table section (Binders, My Cards, Wishlist, a binder's guide) installs it, so
// header-driven sorting stays consistent instead of being reinvented per view.
//
// Why not Qt's built-in setSortingEnabled()? That reorders the QTableWidget's
// *rows* in place, which silently breaks every view here: each one maps a row
// index back to a parallel data vector (loaded_[row], entries_[row]) and rebuilds
// per-row search haystacks. Reordered rows would then point at the wrong records.
//
// Instead this leaves row population to the view. On a header click it hands back
// the clicked column and the order, and the view sorts its OWN data (with a typed
// comparator — numeric dex #, chronological timestamps, condition rank, locale-
// aware text — see compareValues below) and repopulates from scratch, so rows and
// their backing vectors stay aligned.
//
// The cycle is THREE-state, not two: clicking a column sorts it ascending, clicking
// the same column again flips to descending, and a third click drops the sort
// altogether — the view is handed column -1 and returns to its default order, with
// the header's sort indicator cleared. That third click is the only discoverable way
// back: a sort is easy to start by mistake, and in the binder guide the default filed
// order is the only mode that shows the Page/Pocket numbers and page breaks and the
// only one where the arrange actions work. Clicking a different column starts
// ascending again.
//
// It is no longer the ONLY way back, though: this RETURNS a callable that clears the
// sort programmatically, for a view action whose whole point is to put the table back
// (the binder guide's "Scroll to page", which clears the search and the sort together
// before jumping to the selected row). It has to come from in here rather than from a
// free function, because the cycle's state lives INSIDE this helper: a view that merely
// assigned its own sortColumn_ = -1 would leave the header still painting a sort arrow
// over rows it no longer sorts, and would leave the cycle half-finished, so the next
// click on that same column would jump straight to descending instead of starting
// ascending again.
//
// The callable resets that state, clears the indicator, and then reports
// (-1, AscendingOrder) through the view's OWN onSort — so the clear travels the one
// existing path (the view's sortColumn_ assignment plus its repopulate) and a
// programmatic clear is indistinguishable from the user's third click. Keeping the
// view's callback as the single writer of its sort state is the point: a second
// encoding of "what happens when the sort clears" is exactly what would drift. It is a
// no-op when no sort is active (no needless rebuild of a big table), it is safe to hold
// after the table has been destroyed, and it must NEVER be called from inside onSort
// itself (infinite recursion).
//
// The return is deliberately NOT [[nodiscard]]: three of the four call sites have
// nothing to clear programmatically and discard it as a plain statement, which
// [[nodiscard]] would turn into -Werror failures.
//
// The view holds the current (column, order) as its own state and re-applies it on
// every refresh, so sorting survives a reload; a column index of < 0 means "keep
// the natural order the data was loaded in" (the initial and the cleared state).
//
// `resetColumns` names the columns that are NOT sortable fields but the row's position
// itself (the binder guide's Page and Pocket, which only exist in the default order).
// Clicking one is a direct "put it back": it reports column -1 and clears the indicator,
// without the intermediate ascending/descending steps a real column cycles through — so
// the header never paints a sort arrow over a table it did not sort, and its tooltip says
// what it does.
//
// It also gives every column a header tooltip (setHeaderTooltip below) so an elided
// header stays readable and the sort cycle is explained where it is used. A view with
// its own per-column explanation must call setHeaderTooltip AFTER this, since this
// pass would otherwise overwrite it.
enum class HeaderSortRole {
    Sortable,         // an ordinary column: ascending → descending → cleared
    ResetsToDefault,  // a reset column: one click, straight back to the default order
};

inline void setHeaderTooltip(QTableWidget* table, int column, const QString& explanation = {},
                             HeaderSortRole role = HeaderSortRole::Sortable);

inline std::function<void()> installHeaderSort(
    QTableWidget* table, std::function<void(int column, Qt::SortOrder order)> onSort,
    std::vector<int> resetColumns = {}) {
    QHeaderView* header = table->horizontalHeader();
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setSortIndicator(-1, Qt::AscendingOrder);  // no active column initially
    const auto resets = [resetColumns](int column) {
        return std::find(resetColumns.begin(), resetColumns.end(), column) != resetColumns.end();
    };
    for (int column = 0; column < table->columnCount(); ++column) {
        setHeaderTooltip(table, column, {},
                         resets(column) ? HeaderSortRole::ResetsToDefault
                                        : HeaderSortRole::Sortable);
    }
    // The click handler owns the current (column, order) itself rather than reading
    // it back from the header: QHeaderView may mutate its own indicator on a click,
    // so the header is not a reliable source for "what was the previous sort". A
    // shared_ptr keeps the state alive for the connection's lifetime and copyable
    // into the std::function.
    auto state = std::make_shared<std::pair<int, Qt::SortOrder>>(-1, Qt::AscendingOrder);
    // onSort is COPIED into the click handler rather than moved: the returned reset below
    // needs its own copy so a programmatic clear can report through the very same callback.
    QObject::connect(header, &QHeaderView::sectionClicked, table,
                     [table, onSort, state, resets](int column) {
                         // Same column: ascending → descending → cleared. Otherwise
                         // (a different column, or none active) → ascending. A reset
                         // column skips the cycle and goes straight to cleared.
                         int sorted = resets(column) ? -1 : column;
                         Qt::SortOrder order = Qt::AscendingOrder;
                         if (sorted == column && state->first == column) {
                             if (state->second == Qt::AscendingOrder) {
                                 order = Qt::DescendingOrder;
                             } else {
                                 sorted = -1;  // third click: back to the default order
                             }
                         }
                         *state = {sorted, order};
                         table->horizontalHeader()->setSortIndicator(sorted, order);
                         onSort(sorted, order);
                     });
    // The programmatic "put it back" (see the note above). QPointer rather than a raw
    // pointer because, unlike the connection above (which Qt drops with its context
    // object), a returned callable has no lifetime tie to the table.
    return [table = QPointer<QTableWidget>(table), onSort = std::move(onSort), state]() {
        if (table.isNull() || state->first < 0) {
            return;  // gone, or already in the default order — don't rebuild for nothing
        }
        // The same cleared state the third click produces: that path leaves `order`
        // ascending too, so this can't invent one the click path never reaches.
        *state = {-1, Qt::AscendingOrder};
        table->horizontalHeader()->setSortIndicator(-1, Qt::AscendingOrder);
        onSort(-1, Qt::AscendingOrder);
    };
}

// GUI — the tooltip a sortable table's column header carries, composed from three
// parts: the header's own text (so a header the column is too narrow to show in full
// stays readable — the same reason every cell carries its text, see table_cell.h), an
// optional per-column `explanation`, and the hint for what clicking it does. Composing
// rather than appending means it can be re-applied at any time (BinderView re-sets its
// Page tooltip whenever the binder's pocket grid changes) without the parts piling up.
//
// `role` must match what installHeaderSort was told: a reset column resets on the first
// click, so promising the ascending/descending cycle there would describe a behavior the
// header does not have.
//
// installHeaderSort applies the no-explanation form to every column, so a view only
// calls this itself for a column that has something extra to say — and must do so
// AFTER installHeaderSort, whose own pass would overwrite it.
inline void setHeaderTooltip(QTableWidget* table, int column, const QString& explanation,
                             HeaderSortRole role) {
    QTableWidgetItem* item = table->horizontalHeaderItem(column);
    if (item == nullptr) {
        return;  // a column with no header item has no text to explain
    }
    QStringList parts;
    if (!item->text().isEmpty()) {
        parts << item->text();
    }
    if (!explanation.isEmpty()) {
        parts << explanation;
    }
    parts << (role == HeaderSortRole::ResetsToDefault
                  ? QObject::tr("Click to put the table back in its default order.")
                  : QObject::tr("Click to sort by this column; click again to reverse it, "
                                "and once more to restore the default order."));
    item->setToolTip(tooltipText(parts.join(QStringLiteral("\n\n"))));
}

// GUI — three-way compare for a sort comparator: -1 / 0 / +1 for a < / == / >.
// Works for any less-than-comparable field (ints like a dex number, chrono
// Timestamps); string columns use QString::localeAwareCompare directly, which
// already returns this shape. A view's comparator computes one of these for the
// active column, then applies the order: `order == Qt::AscendingOrder ? cmp < 0
// : cmp > 0`.
template <class T>
int compareValues(const T& a, const T& b) {
    if (a < b) {
        return -1;
    }
    if (b < a) {
        return 1;
    }
    return 0;
}

// GUI — direction-aware three-way compare (compareValues shape) for an OPTIONAL
// column value that must sink its unset (nullopt) rows to the BOTTOM in BOTH sort
// directions — "no data" isn't a low value, so it belongs last whichever way the
// column is sorted. applyColumnSort below flips the comparator's sign for a descending
// sort, so this pre-inverts the set-vs-unset case: `ascending` picks the sign that keeps
// the unset operand last after that flip. Two present values are compared with `cmp`
// (e.g. compareValues for ranks, localeAwareCompare for text); two unset are equal. This
// is the single home of that subtle logic, shared by every view with an optional column
// (OwnedCardsView's Condition/Rarity/Foil, the binder guide's copy columns) so the
// pre-invert can never drift out of lockstep with applyColumnSort's flip.
template <class T, class Cmp>
int compareOptional(const std::optional<T>& a, const std::optional<T>& b, bool ascending,
                    Cmp cmp) {
    if (a && b) {
        return cmp(*a, *b);
    }
    if (!a && !b) {
        return 0;
    }
    const int setBeforeUnset = ascending ? -1 : 1;
    return a ? setBeforeUnset : -setBeforeUnset;
}

// GUI — sort a view's backing vector by the active header column/order, given a
// per-column three-way comparator. This is the shared shell every table view runs
// in its refresh()/reload() after installHeaderSort reports a click, so the
// stable-sort + ascending/descending plumbing lives in one place and only the
// switch-on-column body differs per view. `columnCompare(a, b, column)` returns
// the compareValues shape (-1/0/+1) for that column; a `column < 0` means the view
// is unsorted, so the natural load order is kept untouched. std::stable_sort keeps
// items equal on the sort key in their prior relative order.
template <class T, class Compare>
void applyColumnSort(std::vector<T>& items, int column, Qt::SortOrder order,
                     Compare columnCompare) {
    if (column < 0) {
        return;  // unsorted: keep the natural load order
    }
    const bool ascending = order == Qt::AscendingOrder;
    std::stable_sort(items.begin(), items.end(), [&](const T& a, const T& b) {
        const int cmp = columnCompare(a, b, column);
        return ascending ? cmp < 0 : cmp > 0;
    });
}

// GUI — the decorate-sort-reorder shell in one call: sort `items` by the active
// header column/order without recomputing sort keys per comparison. `keyFn(item)`
// returns a small key struct built ONCE per row (a catalog lookup, a QString
// allocation); `keyCompare(a, b, column)` three-way-compares two keys for a column
// (the compareValues shape). Internally it decorates each item's key with the item's
// index, sorts the decorated vector via applyColumnSort, then gathers `items` into
// key order by index — so it's O(n) key construction plus O(n log n) comparisons on
// cheap keys, not O(n log n) key rebuilds. A `column < 0` leaves `items` in its
// natural load order. This is the single home of the pattern BinderView, BindersPage,
// and OwnedCardsView previously spelled out by hand; WishlistView, whose row struct
// already carries its keys, sorts in place with applyColumnSort directly.
template <class T, class KeyFn, class KeyCompare>
void sortByKeys(std::vector<T>& items, int column, Qt::SortOrder order, KeyFn keyFn,
                KeyCompare keyCompare) {
    if (column < 0) {
        return;  // unsorted: keep the natural load order
    }
    using Key = std::decay_t<decltype(keyFn(std::declval<const T&>()))>;
    struct Decorated {
        Key key;
        std::size_t index;
    };
    std::vector<Decorated> decorated;
    decorated.reserve(items.size());
    for (std::size_t i = 0; i < items.size(); ++i) {
        decorated.push_back({keyFn(items[i]), i});
    }
    applyColumnSort(decorated, column, order,
                    [&](const Decorated& a, const Decorated& b, int col) {
                        return keyCompare(a.key, b.key, col);
                    });
    std::vector<T> sorted;
    sorted.reserve(items.size());
    for (const Decorated& d : decorated) {
        sorted.push_back(std::move(items[d.index]));
    }
    items = std::move(sorted);
}

// GUI — the same two shells, but reordering `items` FROM a pristine copy of the load
// order rather than from wherever the last sort left it. A view whose rows are its own
// vector sorts that vector in PLACE, so once a sort can be CLEARED — and with the
// three-state cycle it can be, from any column, at any time — "column < 0 keeps the
// natural load order" only holds if that order is still around to keep. It isn't: the
// previous sort destroyed it. So a view like that holds the load order in a second
// vector and reorders through these, which restore it first.
//
// It is load-bearing in the binder guide, where the cleared state also re-enables the
// filed-order-only behaviour (Page/Pocket numbers, page breaks, Insert blank, Move…):
// reading a rarity-sorted row list as the filed order would print wrong pocket
// coordinates and let a confirmed Move persist an arrangement the binder was never in.
// It also makes ties deterministic — stable_sort breaks them by the natural order every
// time, rather than by whichever column happened to be clicked before.
//
// `natural` must be a distinct vector, never `items` itself.
template <class T, class KeyFn, class KeyCompare>
void sortByKeys(std::vector<T>& items, const std::vector<T>& natural, int column,
                Qt::SortOrder order, KeyFn keyFn, KeyCompare keyCompare) {
    items = natural;
    sortByKeys(items, column, order, std::move(keyFn), std::move(keyCompare));
}

template <class T, class Compare>
void applyColumnSort(std::vector<T>& items, const std::vector<T>& natural, int column,
                     Qt::SortOrder order, Compare columnCompare) {
    items = natural;
    applyColumnSort(items, column, order, std::move(columnCompare));
}

}  // namespace pokedex
