#include "gui/views/binders_window.h"

#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QVBoxLayout>
#include <QVariant>

#include <exception>

#include "core/app/binder_service.h"
#include "core/domain/card_binder.h"
#include "gui/views/binder_editor_dialog.h"
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

BindersWindow::BindersWindow(BinderService& service, const QString& collectionPath,
                             QWidget* parent)
    : QWidget(parent), service_(service) {
    setWindowTitle(tr("Pokedex TCG — Binders"));
    resize(800, 600);

    auto* heading = new QLabel(tr("Your binders"), this);

    list_ = new QListWidget(this);

    auto* newButton = new QPushButton(tr("New…"), this);
    renameButton_ = new QPushButton(tr("Rename…"), this);
    removeButton_ = new QPushButton(tr("Remove…"), this);

    connect(newButton, &QPushButton::clicked, this, &BindersWindow::createBinder);
    connect(renameButton_, &QPushButton::clicked, this, &BindersWindow::renameSelected);
    connect(removeButton_, &QPushButton::clicked, this, &BindersWindow::removeSelected);
    connect(list_, &QListWidget::itemSelectionChanged, this, &BindersWindow::updateButtonState);

    auto* buttons = new QHBoxLayout;
    buttons->addWidget(newButton);
    buttons->addWidget(renameButton_);
    buttons->addWidget(removeButton_);
    buttons->addStretch();

    auto* pathLabel = new QLabel(tr("Collection: %1").arg(collectionPath), this);
    pathLabel->setToolTip(collectionPath);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    QFont pathFont = pathLabel->font();
    pathFont.setPointSize(qMax(1, pathFont.pointSize() - 2));
    pathLabel->setFont(pathFont);
    pathLabel->setEnabled(false);  // muted, it's a reference detail not an action

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(heading);
    layout->addWidget(list_);
    layout->addLayout(buttons);
    layout->addWidget(pathLabel);

    refresh();
}

void BindersWindow::refresh() {
    list_->clear();
    try {
        for (const CardBinder& binder : service_.list()) {
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

void BindersWindow::updateButtonState() {
    const bool hasSelection = list_->currentItem() != nullptr &&
                              !list_->selectedItems().isEmpty();
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
