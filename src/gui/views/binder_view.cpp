#include "gui/views/binder_view.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <exception>

#include "core/app/binder_guide_service.h"
#include "gui/views/region_labels.h"
#include "gui/views/status_labels.h"

namespace pokedex {

namespace {

QString headingText(const CardBinder& binder) {
    const QString name = QString::fromStdString(binder.name);
    if (binder.pokemonRegion) {
        return QStringLiteral("%1 — %2").arg(name, regionLabel(*binder.pokemonRegion));
    }
    return name;
}

// A non-editable cell. Text is fixed for the life of the page (status never
// changes here), so items are built once and only filtered by row visibility.
QTableWidgetItem* cell(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(item->flags() & ~Qt::ItemIsEditable);
    return item;
}

}  // namespace

BinderView::BinderView(BinderGuideService& guide, const CardBinder& binder, QWidget* parent)
    : QWidget(parent) {
    auto* backButton = new QPushButton(tr("← Back"), this);
    auto* heading = new QLabel(headingText(binder), this);

    connect(backButton, &QPushButton::clicked, this, &BinderView::backRequested);

    // A top bar: Back on the left, the binder's name beside it.
    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search Pokémon…"));
    search_->setClearButtonEnabled(true);

    // A read-only three-column table: dex number, name, status. Whole-row
    // selection, no editing; the Pokémon column takes the slack.
    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon"), tr("Status")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);

    connect(search_, &QLineEdit::textChanged, this, &BinderView::applyFilter);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(topBar);
    layout->addWidget(search_);
    layout->addWidget(table_);

    // Compute the guide once; the search box only re-filters this cached vector,
    // never re-queries. A failure here (e.g. the workspace went away) is reported
    // and leaves an empty table rather than crashing.
    try {
        entries_ = guide.buildEntries(binder);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not open this binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }

    // Build every row once. Rows never change after this (status is fixed for the
    // life of the page), so filtering just toggles row visibility — no
    // per-keystroke allocation. entries_ and table rows stay 1:1 and aligned.
    table_->setRowCount(static_cast<int>(entries_.size()));
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const CardBinderEntry& entry = entries_[i];
        auto* number = cell(QString::number(entry.pokemon.dexNumber));
        number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(i, 0, number);
        table_->setItem(i, 1, cell(QString::fromStdString(entry.pokemon.name)));
        table_->setItem(i, 2, cell(statusLabel(entry.status)));
    }
    applyFilter(QString());
}

void BinderView::applyFilter(const QString& filter) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const QString name = QString::fromStdString(entries_[i].pokemon.name);
        const bool visible = filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive);
        table_->setRowHidden(i, !visible);
    }
}

}  // namespace pokedex
