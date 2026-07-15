#include "gui/views/binder_view.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QString>
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

QString rowText(const CardBinderEntry& entry) {
    return QStringLiteral("#%1  %2 — %3")
        .arg(entry.pokemon.dexNumber)
        .arg(QString::fromStdString(entry.pokemon.name), statusLabel(entry.status));
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

    list_ = new QListWidget(this);

    connect(search_, &QLineEdit::textChanged, this, &BinderView::renderList);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(topBar);
    layout->addWidget(search_);
    layout->addWidget(list_);

    // Compute the guide once; the search box only re-filters this cached vector,
    // never re-queries. A failure here (e.g. the workspace went away) is reported
    // and leaves an empty list rather than crashing.
    try {
        entries_ = guide.buildEntries(binder);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Pokedex TCG"),
                              tr("Could not open this binder:\n%1")
                                  .arg(QString::fromUtf8(e.what())));
    }

    // Build every row once. Rows never change after this (status is fixed for the
    // life of the page), so filtering just toggles visibility — no per-keystroke
    // allocation. entries_ and the list items stay 1:1 and index-aligned.
    for (const CardBinderEntry& entry : entries_) {
        new QListWidgetItem(rowText(entry), list_);
    }
    renderList(QString());
}

void BinderView::renderList(const QString& filter) {
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const QString name = QString::fromStdString(entries_[i].pokemon.name);
        const bool visible = filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive);
        list_->item(i)->setHidden(!visible);
    }
}

}  // namespace pokedex
