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
#include "gui/views/datetime_label.h"
#include "gui/views/region_labels.h"
#include "gui/views/sortable_table.h"
#include "gui/views/table_cell.h"
#include "gui/views/toast.h"

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
                         CardSearchService& cardSearch, CardCopyService& cardCopies,
                         CardImageStore& cardImages, const QString& collectionPath,
                         QWidget* parent)
    : QWidget(parent),
      service_(service),
      guide_(guide),
      wishlist_(wishlist),
      media_(media),
      cardSearch_(cardSearch),
      cardCopies_(cardCopies),
      cardImages_(cardImages) {
    // Page 0 of the stack: the binder table with its actions. Built into its own
    // container so opening a binder can swap it out for the binder guide in place.
    auto* listPage = new QWidget(this);

    // A read-only two-column table: binder name (stretches) and region. Whole-row
    // single selection, no editing, no vertical header.
    table_ = new QTableWidget(listPage);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("Binder"), tr("Region"), tr("Added"), tr("Updated")});
    table_->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
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
    // Clicking a header sorts by that column; store the choice and repopulate from the
    // cached binders_ — a pure reorder, no re-query.
    installHeaderSort(table_, [this](int column, Qt::SortOrder order) {
        sortColumn_ = column;
        sortOrder_ = order;
        repopulate();
    });

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
    // (Re)load the binders from storage, then rebuild the table. A header-sort goes
    // through repopulate() directly, so reordering never re-hits storage.
    try {
        binders_ = service_.list();
    } catch (const std::exception& e) {
        binders_.clear();
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not load your binders:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }
    repopulate();
}

void BindersPage::repopulate() {
    // Remember the selected binder by identity, not row index: a header-sort
    // reorders the rows, so the same index would afterwards point at a different
    // binder — and Rename/Remove act on the current row. Re-selecting it below at
    // its new row keeps a destructive action from silently targeting the wrong one.
    const std::string previouslySelected = selectedId();
    table_->setRowCount(0);
    sortBinders();
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
        table_->setItem(i, 2, cell(dateTimeLabel(binder.insertedAt)));
        table_->setItem(i, 3, cell(dateTimeLabel(binder.updatedAt)));
    }
    // Restore the selection at its new row (a no-op if it was removed).
    if (!previouslySelected.empty()) {
        for (int i = 0; i < static_cast<int>(binders_.size()); ++i) {
            if (binders_[i].id == previouslySelected) {
                table_->setCurrentCell(i, 0);
                break;
            }
        }
    }
    updateButtonState();
}

void BindersPage::sortBinders() {
    // The name and region columns each allocate a QString, so precompute each row's keys
    // once (via sortByKeys) rather than rebuilding them for both operands on every
    // comparison. A sortColumn_ < 0 keeps the natural load order.
    struct Key {
        QString name;
        QString region;
        Timestamp insertedAt;
        Timestamp updatedAt;
    };
    sortByKeys(
        binders_, sortColumn_, sortOrder_,
        [](const CardBinder& b) {
            return Key{QString::fromStdString(b.name),
                       b.pokemonRegion ? regionLabel(*b.pokemonRegion) : QString(),
                       b.insertedAt, b.updatedAt};
        },
        [](const Key& a, const Key& b, int column) -> int {
            switch (column) {
                case 0:
                    return a.name.localeAwareCompare(b.name);
                case 1:
                    return a.region.localeAwareCompare(b.region);
                case 2:
                    return compareValues(a.insertedAt, b.insertedAt);
                case 3:
                    return compareValues(a.updatedAt, b.updatedAt);
            }
            return 0;
        });
}

void BindersPage::createBinder() {
    BinderEditorDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }
    try {
        service_.create(dialog.name(), dialog.region());
        showToast(this, tr("Binder “%1” created.")
                            .arg(QString::fromStdString(dialog.name())));
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
        showToast(this, tr("Binder renamed to “%1”.").arg(entered));
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
        showToast(this, tr("Binder removed."));
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
    auto* view =
        new BinderView(guide_, *it, wishlist_, media_, cardSearch_, cardCopies_, cardImages_,
                       service_);
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
