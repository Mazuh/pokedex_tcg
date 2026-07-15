#include "gui/views/binders_window.h"

#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <exception>

#include "core/app/binder_service.h"
#include "core/domain/card_binder.h"
#include "gui/views/binder_editor_dialog.h"
#include "gui/views/binder_view.h"
#include "gui/views/region_labels.h"

namespace pokedex {

namespace {
// Roles under which each row stashes the raw binder fields, so actions target
// the right binder and edit its true name — never the "name — region" display
// text (pre-filling rename from that would fold the region suffix back into the
// name and compound on every rename).
constexpr int kIdRole = Qt::UserRole;
constexpr int kNameRole = Qt::UserRole + 1;

QString rowText(const CardBinder& binder) {
    const QString name = QString::fromStdString(binder.name);
    if (binder.pokemonRegion) {
        return QStringLiteral("%1 — %2").arg(name, regionLabel(*binder.pokemonRegion));
    }
    return name;
}
}  // namespace

BindersWindow::BindersWindow(BinderService& service, BinderGuideService& guide,
                             const QString& collectionPath, QWidget* parent)
    : QWidget(parent), service_(service), guide_(guide) {
    setWindowTitle(tr("Pokedex TCG — Binders"));
    resize(800, 600);

    // Page 0 of the stack: the binder list with its actions. Built into its own
    // container so opening a binder can swap it out for the binder page in place.
    auto* listPage = new QWidget(this);

    auto* heading = new QLabel(tr("Your binders"), listPage);

    list_ = new QListWidget(listPage);

    auto* newButton = new QPushButton(tr("New…"), listPage);
    openButton_ = new QPushButton(tr("Open…"), listPage);
    renameButton_ = new QPushButton(tr("Rename…"), listPage);
    removeButton_ = new QPushButton(tr("Remove…"), listPage);

    connect(newButton, &QPushButton::clicked, this, &BindersWindow::createBinder);
    connect(openButton_, &QPushButton::clicked, this, &BindersWindow::openSelected);
    connect(renameButton_, &QPushButton::clicked, this, &BindersWindow::renameSelected);
    connect(removeButton_, &QPushButton::clicked, this, &BindersWindow::removeSelected);
    connect(list_, &QListWidget::itemSelectionChanged, this, &BindersWindow::updateButtonState);
    // Double-clicking a binder opens it, the usual list-activation gesture.
    connect(list_, &QListWidget::itemActivated, this, &BindersWindow::openSelected);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(openButton_);
    buttons->addWidget(renameButton_);
    buttons->addWidget(removeButton_);
    buttons->addStretch();

    auto* pathLabel = new QLabel(tr("Collection: %1").arg(collectionPath), listPage);
    pathLabel->setToolTip(collectionPath);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont pathFont = pathLabel->font();
    pathFont.setPointSize(qMax(1, pathFont.pointSize() - 2));
    pathLabel->setFont(pathFont);
    pathLabel->setEnabled(false);  // muted, it's a reference detail not an action

    auto* pageLayout = new QVBoxLayout(listPage);
    pageLayout->addWidget(heading);
    pageLayout->addWidget(list_);
    pageLayout->addLayout(buttons);
    pageLayout->addWidget(pathLabel);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(listPage);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    refresh();
}

void BindersWindow::refresh() {
    list_->clear();
    binders_.clear();
    try {
        binders_ = service_.list();
        for (const CardBinder& binder : binders_) {
            auto* item = new QListWidgetItem(rowText(binder), list_);
            item->setData(kIdRole, QString::fromStdString(binder.id));
            item->setData(kNameRole, QString::fromStdString(binder.name));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not load your binders:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }
    updateButtonState();
}

void BindersWindow::createBinder() {
    BinderEditorDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    try {
        service_.create(dialog.name(), dialog.region());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not create the binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }
    refresh();
}

void BindersWindow::renameSelected() {
    const std::string id = selectedId();
    if (id.empty()) {
        return;
    }
    QListWidgetItem* item = list_->selectedItems().first();
    bool ok = false;
    // Pre-fill with the raw name (kNameRole), not the "name — region" row text.
    const QString current = item->data(kNameRole).toString();
    const QString entered =
        QInputDialog::getText(this, tr("Rename Binder"), tr("New name:"), QLineEdit::Normal,
                              current, &ok);
    if (!ok) {
        return;
    }
    try {
        service_.rename(id, entered.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not rename the binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }
    refresh();
}

void BindersWindow::removeSelected() {
    const std::string id = selectedId();
    if (id.empty()) {
        return;
    }
    const auto choice = QMessageBox::question(
        this, tr("Remove Binder"),
        tr("Remove this binder? Cards filed in it are kept — only the binder and "
           "their assignment to it go away."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }
    try {
        service_.remove(id);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not remove the binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }
    refresh();
}

void BindersWindow::openSelected() {
    const std::string id = selectedId();
    if (id.empty()) {
        return;
    }
    // Recover the full CardBinder (region included) from the cached list, so the
    // guide can list the whole region — not just what the row text shows.
    const auto it = std::find_if(binders_.begin(), binders_.end(),
                                 [&](const CardBinder& b) { return b.id == id; });
    if (it == binders_.end()) {
        return;  // selection no longer in the list (refreshed underneath us)
    }
    // Navigate in place: push a binder page onto the stack and show it. Back
    // returns to the list and disposes of the page, so each open starts fresh
    // (recomputing the guide) rather than showing a stale one.
    const QString binderName = QString::fromStdString(it->name);
    auto* view = new BinderView(guide_, *it);
    connect(view, &BinderView::backRequested, this, [this, view]() {
        stack_->setCurrentIndex(0);
        setWindowTitle(tr("Pokedex TCG — Binders"));
        stack_->removeWidget(view);
        view->deleteLater();
    });
    stack_->addWidget(view);
    stack_->setCurrentWidget(view);
    setWindowTitle(tr("Pokedex TCG — %1").arg(binderName));
}

void BindersWindow::updateButtonState() {
    const bool hasSelection = list_->currentItem() != nullptr &&
                              !list_->selectedItems().isEmpty();
    openButton_->setEnabled(hasSelection);
    renameButton_->setEnabled(hasSelection);
    removeButton_->setEnabled(hasSelection);
}

std::string BindersWindow::selectedId() const {
    const QList<QListWidgetItem*> selected = list_->selectedItems();
    if (selected.isEmpty()) {
        return {};
    }
    return selected.first()->data(kIdRole).toString().toStdString();
}

}  // namespace pokedex
