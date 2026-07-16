#include "gui/views/binders_page.h"

#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QString>
#include <QStyle>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QVariant>

#include <algorithm>
#include <exception>

#include "core/app/binder_service.h"
#include "core/domain/card_binder.h"
#include "gui/views/binder_editor_dialog.h"
#include "gui/views/binder_view.h"
#include "gui/views/region_labels.h"
#include "gui/views/table_cell.h"

namespace pokedex {

namespace {
// Roles under which the column-0 cell stashes the raw binder fields, so actions
// target the right binder and edit its true name — never the display text (the
// name cell shows the raw name, but rename reads the role so it stays robust if
// the display ever gains a suffix).
constexpr int kIdRole = Qt::UserRole;
constexpr int kNameRole = Qt::UserRole + 1;
}  // namespace

BindersPage::BindersPage(BinderService& service, BinderGuideService& guide,
                         WishlistService& wishlist, MediaService& media,
                         CardSearchService& cardSearch, const QString& collectionPath,
                         QWidget* parent)
    : QWidget(parent),
      service_(service),
      guide_(guide),
      wishlist_(wishlist),
      media_(media),
      cardSearch_(cardSearch) {
    // Page 0 of the stack: the binder table with its actions. Built into its own
    // container so opening a binder can swap it out for the binder guide in place.
    auto* listPage = new QWidget(this);

    // A read-only two-column table: binder name (stretches) and region. Whole-row
    // single selection, no editing, no vertical header.
    table_ = new QTableWidget(listPage);
    table_->setColumnCount(2);
    table_->setHorizontalHeaderLabels({tr("Binder"), tr("Region")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    // Cell padding so content clears the edges and the overlay scrollbar.
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

    auto* newButton = new QPushButton(tr("New…"), listPage);
    openButton_ = new QPushButton(tr("Open…"), listPage);
    renameButton_ = new QPushButton(tr("Rename…"), listPage);
    removeButton_ = new QPushButton(tr("Remove…"), listPage);
    newButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    openButton_->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
    removeButton_->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));

    connect(newButton, &QPushButton::clicked, this, &BindersPage::createBinder);
    connect(openButton_, &QPushButton::clicked, this, &BindersPage::openSelected);
    connect(renameButton_, &QPushButton::clicked, this, &BindersPage::renameSelected);
    connect(removeButton_, &QPushButton::clicked, this, &BindersPage::removeSelected);
    connect(table_, &QTableWidget::itemSelectionChanged, this, &BindersPage::updateButtonState);
    // Double-clicking a binder opens it, the usual list-activation gesture.
    connect(table_, &QTableWidget::cellActivated, this, &BindersPage::openSelected);

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
    pageLayout->setContentsMargins(16, 12, 16, 12);  // don't hug the section edges
    pageLayout->addWidget(table_);
    pageLayout->addLayout(buttons);
    pageLayout->addWidget(pathLabel);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(listPage);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(stack_);

    refresh();
}

void BindersPage::refresh() {
    table_->setRowCount(0);
    binders_.clear();
    try {
        binders_ = service_.list();
        table_->setRowCount(static_cast<int>(binders_.size()));
        for (int i = 0; i < static_cast<int>(binders_.size()); ++i) {
            const CardBinder& binder = binders_[i];
            auto* nameCell = cell(QString::fromStdString(binder.name));
            nameCell->setData(kIdRole, QString::fromStdString(binder.id));
            nameCell->setData(kNameRole, QString::fromStdString(binder.name));
            table_->setItem(i, 0, nameCell);
            // A region-less binder passes an empty string; cell() renders it as
            // an em-dash.
            const QString region =
                binder.pokemonRegion ? regionLabel(*binder.pokemonRegion) : QString();
            table_->setItem(i, 1, cell(region));
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not load your binders:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }
    updateButtonState();
}

void BindersPage::createBinder() {
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

void BindersPage::renameSelected() {
    const std::string id = selectedId();
    if (id.empty()) {
        return;
    }
    QTableWidgetItem* item = table_->item(table_->currentRow(), 0);
    bool ok = false;
    // Pre-fill with the raw name (kNameRole), not the display cell text.
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

void BindersPage::removeSelected() {
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

void BindersPage::openSelected() {
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
    // Navigate in place: push a binder guide onto the stack and show it. Back
    // returns to the list and disposes of the page, so each open starts fresh
    // (recomputing the guide) rather than showing a stale one.
    auto* view = new BinderView(guide_, *it, wishlist_, media_, cardSearch_);
    connect(view, &BinderView::backRequested, this, [this, view]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(view);
        view->deleteLater();
    });
    stack_->addWidget(view);
    stack_->setCurrentWidget(view);
}

void BindersPage::updateButtonState() {
    const bool hasSelection = table_->currentRow() >= 0 &&
                              !table_->selectedItems().isEmpty();
    openButton_->setEnabled(hasSelection);
    renameButton_->setEnabled(hasSelection);
    removeButton_->setEnabled(hasSelection);
}

std::string BindersPage::selectedId() const {
    const int row = table_->currentRow();
    if (row < 0 || table_->selectedItems().isEmpty()) {
        return {};
    }
    QTableWidgetItem* item = table_->item(row, 0);
    return item ? item->data(kIdRole).toString().toStdString() : std::string{};
}

}  // namespace pokedex
