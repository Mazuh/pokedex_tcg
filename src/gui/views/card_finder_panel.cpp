#include "gui/views/card_finder_panel.h"

#include <QAbstractItemView>
#include <QCompleter>
#include <QEvent>
#include <QFontMetrics>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QScrollBar>
#include <QSize>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "gui/services/card_search_service.h"
#include "gui/views/scaled_pixmap.h"
#include "gui/views/splitter_style.h"

namespace pokedex {

namespace {

// Chunk + prefetch tuning mirrors PokemonListView's infinite scroll.
constexpr int kChunkSize = 20;
constexpr int kPrefetchMargin = 64;

// The card thumbnails are portrait; size the row icon to that aspect.
constexpr int kThumbW = 48;
constexpr int kThumbH = 66;

// The completer entry for a set: "CODE — Name", or just "Name" for a code-less
// set. Built when populating the completer and reverse-matched when a set is
// picked, so it must be formatted in exactly one place — a divergence between the
// two would leave picked entries matching no set (silently doing nothing).
QString setEntryLabel(const CardSetInfo& s) {
    const QString name = QString::fromStdString(s.name);
    const QString code = QString::fromStdString(s.ptcgoCode);
    return code.isEmpty() ? name : QStringLiteral("%1 — %2").arg(code, name);
}

}  // namespace

CardFinderPanel::CardFinderPanel(CardSearchService& search, int dexNumber,
                                 QString speciesName, QWidget* parent)
    : QWidget(parent),
      search_(search),
      dexNumber_(dexNumber),
      speciesName_(std::move(speciesName)) {
    init(QString());  // species mode never seeds the field
}

CardFinderPanel::CardFinderPanel(CardSearchService& search, NameSearchMode,
                                 QString initialQuery, QWidget* parent)
    : QWidget(parent), search_(search), dexNumber_(0), nameMode_(true) {
    init(initialQuery);
}

void CardFinderPanel::init(const QString& initialQuery) {
    // --- Search field + results list (left) --------------------------------
    auto* listPane = new QWidget(this);
    searchField_ = new QLineEdit(listPane);
    searchField_->setPlaceholderText(nameMode_ ? tr("Find a card by name…")
                                               : tr("Find by set — code or name…"));
    searchField_->setClearButtonEnabled(true);
    connect(searchField_, &QLineEdit::textEdited, this, &CardFinderPanel::onSearchTextChanged);

    status_ = new QLabel(listPane);
    status_->setEnabled(false);  // muted status/hint text
    status_->setWordWrap(true);

    printings_ = new QListWidget(listPane);
    printings_->setIconSize(QSize(kThumbW, kThumbH));
    printings_->setUniformItemSizes(true);
    printings_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    printings_->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(printings_, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current, QListWidgetItem*) {
                if (current != nullptr) {
                    selectCandidate(current->data(Qt::UserRole).toInt());
                }
            });
    // Infinite scroll: append the next chunk as the user nears the bottom.
    connect(printings_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        QScrollBar* bar = printings_->verticalScrollBar();
        if (value >= bar->maximum() - kPrefetchMargin &&
            loadedCount_ < static_cast<int>(candidates_.size())) {
            loadMore();
        }
    });
    printings_->viewport()->installEventFilter(this);

    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(searchField_);
    listLayout->addWidget(status_);
    listLayout->addWidget(printings_);

    // --- Preview (right): the selected card, larger ------------------------
    auto* previewPane = new QWidget(this);
    preview_ = new QLabel(previewPane);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumWidth(180);
    preview_->setWordWrap(true);
    preview_->installEventFilter(this);  // rescale the image when the pane resizes
    previewLayout_ = new QVBoxLayout(previewPane);
    previewLayout_->setContentsMargins(0, 0, 0, 0);
    previewLayout_->addWidget(preview_, /*stretch=*/1);  // footer (if any) sits below

    // --- Assemble: list ⇄ preview, both draggable --------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(listPane);
    splitter->addWidget(previewPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({300, 260});
    thinDivider(splitter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    // --- Wire the search service -------------------------------------------
    connect(&search_, &CardSearchService::printingsReady, this,
            &CardFinderPanel::onPrintingsReady);
    connect(&search_, &CardSearchService::printingsFailed, this,
            &CardFinderPanel::onPrintingsFailed);
    connect(&search_, &CardSearchService::thumbnailReady, this,
            &CardFinderPanel::onThumbnailReady);
    connect(&search_, &CardSearchService::setsReady, this, &CardFinderPanel::onSetsReady);
    if (!nameMode_) {
        rebuildSetCompleter();  // the set table is warmed at startup, so usually ready
    }

    // Nothing is fetched on open — a species can have hundreds of printings. The
    // user searches by set first (see onSearchTextChanged).
    clearPreview();
    // In name mode a host may seed the field (e.g. the card's stored name) so its
    // printings appear without the user retyping. setText() does not fire textEdited,
    // so kick the search explicitly.
    if (nameMode_ && initialQuery.trimmed().size() >= 3) {
        searchField_->setText(initialQuery);
        searchWith(initialQuery);
    }
    updateStatus();
}

bool CardFinderPanel::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        if (watched == printings_->viewport()) {
            fillViewport();
        } else if (watched == preview_) {
            renderPreview();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CardFinderPanel::onSearchTextChanged(const QString& text) {
    if (text.trimmed().size() < 3) {
        clearResults();  // too short to search
        updateStatus();  // falls back to the "type 3+ chars" hint
        return;
    }
    // Hand the raw filter to the service — it is the single authority on resolving a
    // code/name to sets, waiting for the set table, and returning empty when nothing
    // matches (so we don't duplicate that logic here, and a not-yet-loaded set table
    // no longer dead-ends the finder). The service debounces the request.
    searchWith(text);
}

void CardFinderPanel::clearResults() {
    pendingRequestId_ = 0;  // ignore any in-flight reply for a now-abandoned query
    loading_ = false;
    candidates_.clear();
    loadedCount_ = 0;
    itemById_.clear();
    printings_->clear();
    clearPreview();
}

void CardFinderPanel::onPrintingsReady(std::uint64_t requestId, int dexNumber,
                                       const std::vector<CardCandidate>& cards) {
    Q_UNUSED(dexNumber);
    if (requestId != pendingRequestId_) {
        return;  // not this panel's latest search (another live finder, or superseded)
    }
    loading_ = false;
    candidates_ = cards;
    loadedCount_ = 0;
    itemById_.clear();
    printings_->clear();
    clearPreview();  // fresh results → no card selected yet
    fillViewport();
    updateStatus();
}

void CardFinderPanel::onPrintingsFailed(std::uint64_t requestId, int dexNumber) {
    Q_UNUSED(dexNumber);
    if (requestId != pendingRequestId_) {
        return;
    }
    loading_ = false;
    candidates_.clear();
    loadedCount_ = 0;
    itemById_.clear();
    printings_->clear();
    clearPreview();
    updateStatus();
}

void CardFinderPanel::loadMore() {
    const int total = static_cast<int>(candidates_.size());
    const int next = std::min(loadedCount_ + kChunkSize, total);
    for (int i = loadedCount_; i < next; ++i) {
        const CardCandidate& c = candidates_[i];
        const QString label = QStringLiteral("%1  ·  %2 %3")
                                  .arg(QString::fromStdString(c.name),
                                       QString::fromStdString(c.setName),
                                       QString::fromStdString(c.cardRef.collectorNumber));
        auto* item = new QListWidgetItem(label, printings_);
        item->setData(Qt::UserRole, i);
        item->setIcon(style()->standardIcon(QStyle::SP_FileIcon));  // placeholder
        if (!c.id.empty()) {
            itemById_.insert(QString::fromStdString(c.id), item);
        }
        // Fetch the thumbnail into memory (never cached to disk). It arrives later
        // via thumbnailReady and replaces the placeholder icon.
        search_.fetchThumbnail(QString::fromStdString(c.id),
                               QString::fromStdString(c.imageUrlSmall));
    }
    loadedCount_ = next;
    updateStatus();
}

void CardFinderPanel::fillViewport() {
    if (filling_) {
        return;
    }
    filling_ = true;
    const int total = static_cast<int>(candidates_.size());
    // On the first fill the list is empty, so sizeHintForRow(0) returns -1; fall back
    // to the icon height (a row is at least the thumbnail tall) so we load one chunk,
    // not the whole result set.
    const int measured = printings_->sizeHintForRow(0);
    const int rowHeight = measured > 0 ? measured : kThumbH + 8;
    const int needed = printings_->viewport()->height() / rowHeight + 2;
    while (loadedCount_ < total && loadedCount_ < needed) {
        const int before = loadedCount_;
        loadMore();
        if (loadedCount_ == before) {
            break;
        }
    }
    filling_ = false;
}

void CardFinderPanel::onThumbnailReady(const QString& cardId, const QPixmap& pixmap) {
    if (!previewCardId_.isEmpty() && cardId == previewCardId_) {
        previewPixmap_ = pixmap;  // the large image for the selected card
        renderPreview();
        Q_EMIT previewReady();  // the pixmap is now in hand (selectedPreview() non-null)
        return;
    }
    if (QListWidgetItem* item = itemById_.value(cardId, nullptr)) {
        item->setIcon(QIcon(pixmap));
    }
}

void CardFinderPanel::selectCandidate(int index) {
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return;
    }
    selectedIndex_ = index;
    // Report the pick; a host autofills its form from it (language / condition /
    // ownership stay the user's — the card source cannot supply them).
    Q_EMIT cardSelected(candidates_[index]);
    showPreview(index);
}

void CardFinderPanel::showPreview(int index) {
    const CardCandidate& c = candidates_[index];
    previewPixmap_ = QPixmap();
    const QString url = QString::fromStdString(c.imageUrlLarge.empty() ? c.imageUrlSmall
                                                                       : c.imageUrlLarge);
    if (url.isEmpty()) {
        // No artwork for this printing — say so rather than hang on "Loading card…"
        // forever (fetchThumbnail no-ops on a blank url, so no reply ever clears it).
        previewCardId_.clear();
        preview_->setText(tr("No image available for this card."));
        return;
    }
    // Keyed distinctly from the row thumbnail ("preview:" prefix) so both can be in
    // flight; fetched into memory only (never cached to disk), like every card image.
    previewCardId_ = QStringLiteral("preview:") + QString::fromStdString(c.id);
    preview_->setText(tr("Loading card…"));
    search_.fetchThumbnail(previewCardId_, url);
}

void CardFinderPanel::clearPreview() {
    selectedIndex_ = -1;
    previewCardId_.clear();
    previewPixmap_ = QPixmap();
    preview_->setText(tr("Select a card to preview it."));
    // Drop the list highlight; setCurrentItem(nullptr) fires currentItemChanged with a
    // null current, which selectCandidate ignores (guarded), so this does not recurse.
    printings_->setCurrentItem(nullptr);
    Q_EMIT selectionCleared();
}

void CardFinderPanel::clearSelection() { clearPreview(); }

void CardFinderPanel::renderPreview() {
    if (previewPixmap_.isNull()) {
        return;
    }
    setScaledPixmap(preview_, previewPixmap_);
}

void CardFinderPanel::searchWith(const QString& query) {
    loading_ = true;
    updateStatus();
    // In name mode the field text is the card-name query; in species mode it is a
    // set-code/name filter narrowing the species. The service tags the reply so
    // onPrintingsReady still matches by request id either way.
    pendingRequestId_ = nameMode_ ? search_.searchByName(query, QString())
                                  : search_.searchPrintings(dexNumber_, query);
}

void CardFinderPanel::rebuildSetCompleter() {
    // A "CODE — Name" (or just "Name" for code-less sets) typeahead on the finder's
    // search field. Picking one narrows the search to that set (and lets a host fill
    // its own set fields via setChosen).
    QStringList entries;
    for (const CardSetInfo& s : search_.sets()) {
        if (s.name.empty() && s.ptcgoCode.empty()) {
            continue;
        }
        entries << setEntryLabel(s);
    }
    entries.removeDuplicates();
    entries.sort(Qt::CaseInsensitive);

    auto* completer = new QCompleter(entries, searchField_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    // Entries can be long and differ only by a trailing year (McDonald's Collection
    // 2019/2020/…); size the popup to the longest one (capped) and never elide.
    completer->popup()->setTextElideMode(Qt::ElideNone);
    const QFontMetrics fm(searchField_->font());
    int widest = 0;
    for (const QString& entry : entries) {
        widest = std::max(widest, fm.horizontalAdvance(entry));
    }
    if (widest > 0) {
        completer->popup()->setMinimumWidth(std::min(widest + 32, 460));
    }
    searchField_->setCompleter(completer);
    // Connect AFTER setCompleter so this slot runs after QLineEdit's own handler
    // (which would otherwise leave the decorated "CODE — Name" in the field — a
    // string that matches no set, so a later edit would wrongly clear the results).
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this,
            [this](const QString& picked) {
                // Map the picked "CODE — Name"/"Name" entry back to its set, tell any
                // host (setChosen), put the clean set name in the field, and fetch.
                for (const CardSetInfo& s : search_.sets()) {
                    if (setEntryLabel(s) == picked) {
                        Q_EMIT setChosen(s);
                        const QString name = QString::fromStdString(s.name);
                        const QString code = QString::fromStdString(s.ptcgoCode);
                        const QString clean = name.isEmpty() ? code : name;
                        searchField_->setText(clean);  // replace the decorated entry
                        searchWith(clean);
                        return;
                    }
                }
            });
}

void CardFinderPanel::onSetsReady() {
    if (!nameMode_) {
        rebuildSetCompleter();  // name mode has no set completer
    }
    // Re-run the current search now that the set table is available: the user may
    // have typed a filter before it loaded (or a failed startup fetch just retried),
    // in which case the finder was empty and would otherwise stay stale.
    if (searchField_->text().trimmed().size() >= 3) {
        onSearchTextChanged(searchField_->text());
    }
}

void CardFinderPanel::updateStatus() {
    if (loading_) {
        status_->setText(tr("Searching…"));
    } else if (searchField_->text().trimmed().size() < 3) {
        status_->setText(nameMode_
                             ? tr("Find a card by name — type its name (3+ characters).")
                             : tr("Find %1's cards by set — type a code or name (3+ characters).")
                                   .arg(speciesName_));
    } else if (candidates_.empty()) {
        status_->setText(nameMode_
                             ? tr("No cards found by that name — %1").arg(noResultsHint_)
                             : tr("No printings found for that set — %1").arg(noResultsHint_));
    } else {
        status_->setText(tr("Showing %1 of %2 — select a card.")
                             .arg(loadedCount_)
                             .arg(static_cast<int>(candidates_.size())));
    }
}

bool CardFinderPanel::hasSelection() const {
    return selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(candidates_.size());
}

CardCandidate CardFinderPanel::selectedCandidate() const {
    if (!hasSelection()) {
        return CardCandidate{};
    }
    return candidates_[selectedIndex_];
}

QPixmap CardFinderPanel::selectedPreview() const { return previewPixmap_; }

void CardFinderPanel::setNoResultsHint(const QString& hint) {
    noResultsHint_ = hint;
    updateStatus();  // reflect it now if the "no results" message is already showing
}

void CardFinderPanel::setPreviewFooter(QWidget* widget) {
    previewLayout_->addWidget(widget, /*stretch=*/0, Qt::AlignHCenter);
}

}  // namespace pokedex
