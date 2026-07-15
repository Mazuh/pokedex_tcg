#pragma once

#include <QWidget>

#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_binder_entry.h"

class QLineEdit;
class QListWidget;

namespace pokedex {

class BinderGuideService;

// GUI — the "open binder" screen: the binder's guide as a list of its Pokémon,
// each paired with its CollectionStatus, above a live partial-name search box.
// A thin shell over BinderGuideService (the Qt-free verb). Read-only: it computes
// the entries once on construction and only re-filters the cached list as the
// user types — there are no per-entry actions in this slice.
//
// This is an embedded page, not a separate window: BindersWindow shows it inside
// a QStackedWidget and a "Back" button (which emits backRequested) returns to the
// binder list. Opening a binder is in-app navigation, not a modal detour.
class BinderView : public QWidget {
    Q_OBJECT

public:
    BinderView(BinderGuideService& guide, const CardBinder& binder,
               QWidget* parent = nullptr);

Q_SIGNALS:
    // Emitted when the user asks to leave this page (the Back button). The owner
    // navigates back to the binder list and disposes of this view.
    void backRequested();

private:
    // Repopulate the list from the cached entries, keeping only those whose
    // Pokémon name contains `filter` (case-insensitive); an empty filter shows all.
    void renderList(const QString& filter);

    QListWidget* list_;
    QLineEdit* search_;
    std::vector<CardBinderEntry> entries_;
};

}  // namespace pokedex
