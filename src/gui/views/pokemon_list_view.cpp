#include "gui/views/pokemon_list_view.h"

#include <QEvent>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollBar>
#include <QString>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "gui/views/region_labels.h"
#include "gui/views/table_cell.h"

namespace pokedex {

namespace {

// How many rows to append per load. The catalog is ~1000 entries; a chunk keeps
// the initial render cheap and each append unnoticeable.
constexpr int kChunkSize = 20;

// Start loading the next chunk this many pixels before the scrollbar bottom, so
// more rows are ready before the user actually hits the end. The table scrolls
// per pixel (set below), so the scrollbar range — and this margin — are pixels.
constexpr int kPrefetchMargin = 64;

}  // namespace

PokemonListView::PokemonListView(PokemonBrowseService& service, QWidget* parent)
    : QWidget(parent), service_(service) {
    search_ = new QLineEdit(this);
    search_->setPlaceholderText(tr("Search Pokémon…"));
    search_->setClearButtonEnabled(true);

    // A read-only four-column table: dex number, name, region, owned count. Whole-
    // row selection, no editing; the name column takes the slack.
    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({tr("#"), tr("Pokémon"), tr("Region"), tr("Owned")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    // Scroll per pixel so the scrollbar range is in pixels — smoother, and it
    // makes kPrefetchMargin (pixels) meaningful for the near-bottom test below.
    table_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    // Cell padding so content doesn't hug the edges — and, crucially, so the
    // right-aligned Owned column clears the macOS overlay scrollbar that fades
    // in over the right edge while scrolling.
    table_->setStyleSheet("QTableView::item { padding-left: 8px; padding-right: 16px; }");

    countLabel_ = new QLabel(this);
    countLabel_->setEnabled(false);  // muted: a status detail, not an action

    connect(search_, &QLineEdit::textChanged, this, &PokemonListView::applyFilter);
    // Infinite scroll: append the next chunk as the user nears the bottom. The
    // complementary "viewport isn't full yet" case (first show, or the window
    // grew taller than the loaded rows, where no scrollbar exists) is handled by
    // eventFilter() watching the viewport's resize.
    connect(table_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar* bar = table_->verticalScrollBar();
        if (value >= bar->maximum() - kPrefetchMargin &&
            loadedCount_ < static_cast<int>(filtered_.size())) {
            loadMore();
        }
    });
    table_->viewport()->installEventFilter(this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);  // don't hug the section edges
    layout->addWidget(search_);
    layout->addWidget(table_);
    layout->addWidget(countLabel_);

    // Compute the whole catalog once; filtering and lazy loading only work the
    // cached vector, never re-query. applyFilter() seeds filtered_ and loads
    // enough to fill the viewport.
    entries_ = service_.listAll();
    applyFilter();
}

bool PokemonListView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == table_->viewport() && event->type() == QEvent::Resize) {
        fillViewport();
    }
    return QWidget::eventFilter(watched, event);
}

void PokemonListView::applyFilter() {
    const QString filter = search_->text();
    filtered_.clear();
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const PokemonBrowseEntry& entry = entries_[i];
        const QString name = QString::fromStdString(entry.pokemon.name);
        const QString number = QString::number(entry.pokemon.dexNumber);
        const bool visible = filter.isEmpty() ||
                             name.contains(filter, Qt::CaseInsensitive) ||
                             number.contains(filter);
        if (visible) {
            filtered_.push_back(i);
        }
    }
    // Reset to the top and render enough to fill the viewport again. Always
    // refresh the label afterwards so an empty (no-match) result still shows
    // "Showing 0 of 0" rather than the previous filter's stale count.
    loadedCount_ = 0;
    table_->setRowCount(0);
    table_->scrollToTop();
    fillViewport();
    updateCountLabel();
}

void PokemonListView::loadMore() {
    const int total = static_cast<int>(filtered_.size());
    const int next = std::min(loadedCount_ + kChunkSize, total);
    if (next == loadedCount_) {
        return;  // nothing left to load
    }

    // Grow the table and fill only the newly added rows — the rows already shown
    // are left untouched, so scrolling never re-renders what's on screen.
    table_->setRowCount(next);
    for (int row = loadedCount_; row < next; ++row) {
        const PokemonBrowseEntry& entry = entries_[filtered_[row]];
        auto* number = cell(QString::number(entry.pokemon.dexNumber));
        number->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 0, number);
        table_->setItem(row, 1, cell(QString::fromStdString(entry.pokemon.name)));
        table_->setItem(row, 2, cell(regionLabel(entry.pokemon.region)));
        auto* owned = cell(QString::number(entry.ownedCount));
        owned->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table_->setItem(row, 3, owned);
    }
    loadedCount_ = next;
    updateCountLabel();
}

void PokemonListView::fillViewport() {
    if (filling_) {
        return;  // a nested resize (scrollbar appearing) fired mid-fill; ignore it
    }
    filling_ = true;
    const int total = static_cast<int>(filtered_.size());
    // Rows needed to overflow the viewport (so a scrollbar appears and scrolling
    // takes over the loading). Uses the default row height, which is valid even
    // before any row exists and while the vertical header is hidden.
    const int rowHeight = std::max(1, table_->verticalHeader()->defaultSectionSize());
    const int needed = table_->viewport()->height() / rowHeight + 2;
    while (loadedCount_ < total && loadedCount_ < needed) {
        const int before = loadedCount_;
        loadMore();
        if (loadedCount_ == before) {
            break;  // safety: never spin if a load made no progress
        }
    }
    filling_ = false;
}

void PokemonListView::updateCountLabel() {
    countLabel_->setText(
        tr("Showing %1 of %2 Pokémon").arg(loadedCount_).arg(static_cast<int>(filtered_.size())));
}

}  // namespace pokedex
