#pragma once

#include <QHeaderView>
#include <QTableWidget>

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
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
// their backing vectors stay aligned. Clicking a column the first time sorts it
// ascending; clicking the same column again flips to descending; clicking a
// different column starts ascending again. The header's sort indicator is kept in
// sync so the active column and direction stay visible.
//
// The view holds the current (column, order) as its own state and re-applies it on
// every refresh, so sorting survives a reload; a column index of < 0 means "keep
// the natural order the data was loaded in" (the initial, unsorted state).
inline void installHeaderSort(QTableWidget* table,
                              std::function<void(int column, Qt::SortOrder order)> onSort) {
    QHeaderView* header = table->horizontalHeader();
    header->setSectionsClickable(true);
    header->setSortIndicatorShown(true);
    header->setSortIndicator(-1, Qt::AscendingOrder);  // no active column initially
    // The click handler owns the current (column, order) itself rather than reading
    // it back from the header: QHeaderView may mutate its own indicator on a click,
    // so the header is not a reliable source for "what was the previous sort". A
    // shared_ptr keeps the state alive for the connection's lifetime and copyable
    // into the std::function.
    auto state = std::make_shared<std::pair<int, Qt::SortOrder>>(-1, Qt::AscendingOrder);
    QObject::connect(header, &QHeaderView::sectionClicked, table,
                     [table, onSort = std::move(onSort), state](int column) {
                         // Same column already ascending → flip to descending;
                         // otherwise (new column, or was descending) → ascending.
                         Qt::SortOrder order = Qt::AscendingOrder;
                         if (state->first == column && state->second == Qt::AscendingOrder) {
                             order = Qt::DescendingOrder;
                         }
                         *state = {column, order};
                         table->horizontalHeader()->setSortIndicator(column, order);
                         onSort(column, order);
                     });
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

}  // namespace pokedex
