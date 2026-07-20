#pragma once

#include <QWidget>

#include <string>
#include <vector>

#include "core/domain/card_binder.h"

class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace pokedex {

class BinderService;
class BinderGuideService;
class WishlistService;
class MediaService;
class CardSearchService;
class CardCopyService;
class CardImageStore;

// GUI — the Binders section of the main window: a table of the user's binders
// (name + region) with New / Rename / Remove actions, and Open to view a
// binder's guide. Opening a binder navigates in-place — this page swaps the
// binder table for the binder guide inside its own QStackedWidget, and a Back
// button returns — rather than spawning a separate window. It is a thin shell
// over BinderService and BinderGuideService (the Qt-free verbs), translating
// clicks into service calls and exceptions into message boxes, the same way
// FirstRunDialog wraps the install service. The services outlive the page (all
// owned by main()).
//
// It is a section embedded in MainWindow's sidebar/stack, not the top-level
// window, so it does not manage the window title.
class BindersPage : public QWidget {
    Q_OBJECT

public:
    // `collectionPath` is the workspace folder, shown so the user can always see
    // where their collection lives (it may be on a NAS, iCloud, etc.). `media` is
    // forwarded to each opened binder guide so its rows show a detail panel.
    BindersPage(BinderService& service, BinderGuideService& guide, WishlistService& wishlist,
                MediaService& media, CardSearchService& cardSearch, CardCopyService& cardCopies,
                CardImageStore& cardImages, const QString& collectionPath,
                QWidget* parent = nullptr);

private:
    // Re-query the binders from storage, then rebuild the table (via repopulate()).
    // Run when the underlying data may have changed (create/rename/remove, first show).
    void refresh();
    // Sort the already-cached binders_ and rebuild the table rows, preserving the
    // selection by identity. This is the pure in-memory path a header-sort click takes
    // — it never re-hits storage just to reorder rows (refresh() calls it after a query).
    void repopulate();
    // Sort the cached binders_ in place by the active header column/order before
    // repopulate() populates the table. A negative sortColumn_ keeps the service's
    // natural order (the initial, unsorted state).
    void sortBinders();
    void createBinder();
    void renameSelected();
    void removeSelected();
    void openSelected();
    void updateButtonState();

    // The binder id of the current selection, or empty when nothing is selected.
    std::string selectedId() const;

    BinderService& service_;
    BinderGuideService& guide_;
    WishlistService& wishlist_;
    MediaService& media_;
    CardSearchService& cardSearch_;
    CardCopyService& cardCopies_;
    CardImageStore& cardImages_;
    QStackedWidget* stack_;
    QTableWidget* table_;
    // The header-driven sort state, re-applied on every refresh so it survives a
    // reload. -1 = unsorted (keep the service's order); see sortBinders().
    int sortColumn_ = -1;
    Qt::SortOrder sortOrder_ = Qt::AscendingOrder;
    QPushButton* renameButton_;
    QPushButton* removeButton_;
    QPushButton* openButton_;

    // The binders backing the current table rows, in display order — cached from
    // the last refresh() so Open can recover the full CardBinder (region and all)
    // for the selected row without a second query.
    std::vector<CardBinder> binders_;
};

}  // namespace pokedex
