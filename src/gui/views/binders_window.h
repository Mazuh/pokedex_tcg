#pragma once

#include <QString>
#include <QWidget>

#include <string>
#include <vector>

#include "core/domain/card_binder.h"

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;

namespace pokedex {

class BinderService;
class BinderGuideService;

// GUI — the main window for the binder feature: a list of the user's binders with
// New / Rename / Remove actions, and Open to view a binder's guide. Opening a
// binder navigates in-place — the window swaps the list page for the binder page
// inside a QStackedWidget, and a Back button returns — rather than spawning a
// separate window. It is a thin shell over BinderService and BinderGuideService
// (the Qt-free verbs), translating clicks into service calls and exceptions into
// message boxes, the same way FirstRunDialog wraps the install service. The
// services outlive the window (all owned by main()).
class BindersWindow : public QWidget {
    Q_OBJECT

public:
    // `collectionPath` is the workspace folder, shown so the user can always see
    // where their collection lives (it may be on a NAS, iCloud, etc.).
    BindersWindow(BinderService& service, BinderGuideService& guide,
                  const QString& collectionPath, QWidget* parent = nullptr);

private:
    void refresh();
    void createBinder();
    void renameSelected();
    void removeSelected();
    void openSelected();
    void updateButtonState();

    // The binder id of the current selection, or empty when nothing is selected.
    std::string selectedId() const;

    BinderService& service_;
    BinderGuideService& guide_;
    QStackedWidget* stack_;
    QListWidget* list_;
    QPushButton* renameButton_;
    QPushButton* removeButton_;
    QPushButton* openButton_;

    // The binders backing the current list rows, in display order — cached from
    // the last refresh() so Open can recover the full CardBinder (region and all)
    // for the selected row without a second query.
    std::vector<CardBinder> binders_;
};

}  // namespace pokedex
