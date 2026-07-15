#pragma once

#include <QString>
#include <QWidget>

#include <string>

class QListWidget;
class QPushButton;

namespace pokedex {

class BinderService;

// GUI — the main window for the binder CRUD feature: a list of the user's
// binders with New / Rename / Remove actions. It is a thin shell over
// BinderService (the Qt-free verbs), translating clicks into service calls and
// exceptions into message boxes, the same way FirstRunDialog wraps the install
// service. The service outlives the window (both owned by main()).
class BindersWindow : public QWidget {
    Q_OBJECT

public:
    // `collectionPath` is the workspace folder, shown so the user can always see
    // where their collection lives (it may be on a NAS, iCloud, etc.).
    BindersWindow(BinderService& service, const QString& collectionPath,
                  QWidget* parent = nullptr);

private:
    void refresh();
    void createBinder();
    void renameSelected();
    void removeSelected();
    void updateButtonState();

    // The binder id of the current selection, or empty when nothing is selected.
    std::string selectedId() const;

    BinderService& service_;
    QListWidget* list_;
    QPushButton* renameButton_;
    QPushButton* removeButton_;
};

}  // namespace pokedex
