#include "gui/views/card_finder_panel.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSize>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <utility>

#include "core/app/card_catalog_parse.h"
#include "gui/services/card_search_service.h"
#include "gui/views/price_labels.h"
#include "gui/views/scaled_pixmap.h"
#include "gui/views/select_all_line_edit.h"
#include "gui/views/splitter_style.h"

namespace pokedex {

namespace {

// Chunk + prefetch tuning mirrors PokemonListView's infinite scroll. A small
// chunk keeps the up-front load close to just the rows actually visible: each row
// fetches a thumbnail, so a large first chunk would pull images for cards the user
// never scrolls to. fillViewport() still tops up to fill the viewport, so no blank
// space — it just loads (and thumbnails) a handful at a time, then more on scroll.
constexpr int kChunkSize = 5;
constexpr int kPrefetchMargin = 64;

// The card thumbnails are portrait; size the row icon to that aspect.
constexpr int kThumbW = 48;
constexpr int kThumbH = 66;

// The dropdown entry for a set: "CODE — Name", or just "Name" for a code-less set.
// The entry carries the set's id as its data, so nothing is ever matched back by
// this label — it is display only.
QString setEntryLabel(const CardSetInfo& s) {
    const QString name = QString::fromStdString(s.name);
    const QString code = QString::fromStdString(s.ptcgoCode);
    return code.isEmpty() ? name : QStringLiteral("%1 — %2").arg(code, name);
}

// Whether two result sets are the same printings in the same order. The card id is the
// row's identity (it already keys the thumbnail map), so comparing the id sequence is
// what decides whether a reply changes anything the user can see — deliberately NOT a
// full payload comparison, since a re-fetched row's prices may differ while the row
// itself, and the user's pick of it, are unchanged.
bool sameCandidateIds(const std::vector<CardCandidate>& a, const std::vector<CardCandidate>& b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(),
                      [](const CardCandidate& x, const CardCandidate& y) { return x.id == y.id; });
}

// The set carrying `id`, or nullptr when the table doesn't list it.
const CardSetInfo* findSetById(const std::vector<CardSetInfo>& sets, const QString& id) {
    const std::string wanted = id.toStdString();
    for (const CardSetInfo& s : sets) {
        if (s.id == wanted) {
            return &s;
        }
    }
    return nullptr;
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
    // --- Set picker / name field + results list (left) ----------------------
    auto* listPane = new QWidget(this);
    QWidget* input = nullptr;
    if (nameMode_) {
        searchField_ = new SelectAllLineEdit(listPane);
        searchField_->setPlaceholderText(tr("Find a card by name…"));
        searchField_->setClearButtonEnabled(true);
        connect(searchField_, &QLineEdit::textEdited, this,
                &CardFinderPanel::onSearchTextChanged);
        input = searchField_;
    } else {
        // A searchable dropdown, not a text box: type to filter the catalog's sets
        // locally, and only CHOOSING one searches (see the class docstring). Same
        // editable-combo idiom as the wishlist's species picker — NoInsert, so typing
        // can never invent an entry — with the finder's own contains-match completer,
        // since a set is as often recalled by a word in its name as by its code.
        setCombo_ = new QComboBox(listPane);
        setCombo_->setEditable(true);
        setCombo_->setInsertPolicy(QComboBox::NoInsert);
        setCombo_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        setCombo_->setMinimumContentsLength(24);
        setCombo_->lineEdit()->setPlaceholderText(tr("Pick a set…"));
        if (QCompleter* completer = setCombo_->completer()) {
            completer->setCompletionMode(QCompleter::PopupCompletion);
            completer->setCaseSensitivity(Qt::CaseInsensitive);
            completer->setFilterMode(Qt::MatchContains);
        }
        connect(setCombo_, &QComboBox::activated, this, [this](int index) {
            if (index >= 0) {
                chooseSet(setCombo_->itemData(index).toString(), /*emitChosen=*/true);
            }
        });
        input = setCombo_;
    }

    status_ = new QLabel(listPane);
    status_->setEnabled(false);  // muted status/hint text
    status_->setWordWrap(true);

    // Shown only in the two states the user can act on — and the reason each is worth
    // telling apart from an empty result: re-running the exact same query is very often
    // all it takes (the catalog 500s intermittently), and a missing set table leaves
    // nothing to pick from at all.
    retryButton_ = new QPushButton(tr("Retry"), listPane);
    retryButton_->hide();
    connect(retryButton_, &QPushButton::clicked, this, [this]() {
        if (setTableMissing()) {
            search_.reloadSets();  // nothing to search until there are sets to choose
            updateStatus();        // ...which is now loading
            return;
        }
        if (!lastQuery_.isEmpty()) {
            searchWith(lastQuery_);
        }
    });

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
    if (setCombo_ != nullptr) {
        // Installed here, not beside the combo above: eventFilter reads printings_ on
        // every event it sees, so nothing may be filtered before that exists.
        // Watching the COMBO rather than its line edit is load-bearing — QComboBox makes
        // itself the line edit's focus proxy and hands the focus-out on with a direct
        // lineEdit->event() call, which bypasses the filter chain entirely, so a filter
        // on the line edit never sees one.
        setCombo_->installEventFilter(this);  // tidy up abandoned typing
    }

    // Status text and its Retry action share one row, the button trailing so the wrapped
    // status text keeps the left edge it has in every other state.
    auto* statusRow = new QHBoxLayout;
    statusRow->setContentsMargins(0, 0, 0, 0);
    statusRow->addWidget(status_, /*stretch=*/1);
    statusRow->addWidget(retryButton_);

    auto* listLayout = new QVBoxLayout(listPane);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->addWidget(input);
    listLayout->addLayout(statusRow);
    listLayout->addWidget(printings_);

    // --- Preview (right): the selected card, larger ------------------------
    auto* previewPane = new QWidget(this);
    preview_ = new QLabel(previewPane);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumWidth(180);
    preview_->setWordWrap(true);
    preview_->installEventFilter(this);  // rescale the image when the pane resizes
    priceHint_ = new QLabel(previewPane);
    priceHint_->setAlignment(Qt::AlignCenter);
    priceHint_->setWordWrap(true);
    priceHint_->setStyleSheet(QStringLiteral("color: gray; font-size: 11px;"));
    priceHint_->hide();
    previewLayout_ = new QVBoxLayout(previewPane);
    previewLayout_->setContentsMargins(0, 0, 0, 0);
    previewLayout_->addWidget(preview_, /*stretch=*/1);  // footer (if any) sits below
    previewLayout_->addWidget(priceHint_);  // subtle price line, under the image

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
    // A set-table load that ended with nothing changes only what the status says — but
    // it must say it, or the dropdown sits empty with no explanation.
    connect(&search_, &CardSearchService::setsUnavailable, this,
            &CardFinderPanel::updateStatus);
    if (!nameMode_) {
        rebuildSetCombo();  // the set table is warmed at startup, so usually ready
    }

    // Nothing is fetched on open — a species can have hundreds of printings. The
    // user picks a set first (species mode) or types a card name (name mode).
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
    } else if (event->type() == QEvent::FocusOut && watched == setCombo_) {
        // Typing into the dropdown only filters it, so text left behind without a pick
        // ("cri") names nothing and would read as a selection that isn't there. Put the
        // selected set's label back — or clear it when nothing is selected. Skipped while
        // the completer popup is up, since choosing from it is what takes the focus.
        const QCompleter* completer = setCombo_->completer();
        const bool choosing =
            completer != nullptr && completer->popup() != nullptr && completer->popup()->isVisible();
        if (!choosing) {
            const int index = setCombo_->currentIndex();
            setCombo_->lineEdit()->setText(index >= 0 ? setCombo_->itemText(index) : QString());
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CardFinderPanel::onSearchTextChanged(const QString& text) {
    // NAME MODE only — species mode searches from the dropdown, never from typing.
    if (text.trimmed().size() < 3) {
        clearResults();  // too short to search
        updateStatus();  // falls back to the "type 3+ chars" hint
        return;
    }
    searchWith(text);  // the service debounces the request
}

void CardFinderPanel::clearResults() {
    pendingRequestId_ = 0;  // ignore any in-flight reply for a now-abandoned query
    loading_ = false;
    failed_ = false;  // the failed query is abandoned; there is nothing left to retry
    shownSetId_.clear();
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
    failed_ = false;
    // The SAME printings, in the same order, mean nothing on screen needs to change —
    // so leave the rows, the scroll position, the loaded thumbnails and above all the
    // PICKED card alone. Re-running an equivalent search is routine rather than
    // exceptional: picking a set from the completer replaces the typed "cri" with
    // "Chaos Rising", which narrows to the same set and returns the same list. The
    // unconditional rebuild below then dropped the pick (and the card image chosen with
    // it) behind the user's back, and the only cure was to click the very same row
    // again. Identity is the id sequence, not the payload: a fresher reply for a row
    // already on screen is still that row.
    if (!candidates_.empty() && sameCandidateIds(candidates_, cards)) {
        candidates_ = cards;  // same ids in the same order → selectedIndex_ still valid
        updateStatus();
        return;
    }
    candidates_ = cards;
    loadedCount_ = 0;
    itemById_.clear();
    printings_->clear();
    clearPreview();  // genuinely different results → no card selected yet
    fillViewport();
    updateStatus();
}

void CardFinderPanel::onPrintingsFailed(std::uint64_t requestId, int dexNumber) {
    Q_UNUSED(dexNumber);
    if (requestId != pendingRequestId_) {
        return;
    }
    loading_ = false;
    // Remember that the request FAILED, so updateStatus() can say so instead of
    // reporting the empty list below as "no printings found" — the catalog exhausting
    // its retry ladder is not a statement about what the set contains.
    failed_ = true;
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
    // A subtle price line from the prices the search payload already carried — no
    // extra request for a card the user may not even own. Hidden when the payload
    // had no price blocks.
    const QString hint = priceHeadline(c.prices);
    priceHint_->setText(hint);
    priceHint_->setToolTip(
        tr("Rough market estimates: TCGplayer's market price and Cardmarket's trend price. "
           "After you add this card, its Edit page shows the full price breakdown."));
    priceHint_->setVisible(!hint.isEmpty());
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
    priceHint_->clear();
    priceHint_->hide();
    // With no pick, show the host's placeholder image (the copy's current picture) if
    // one was supplied, else the hint. renderPreview() handles the pixmap-vs-text swap.
    renderPreview();
    // Drop the list highlight; setCurrentItem(nullptr) fires currentItemChanged with a
    // null current, which selectCandidate ignores (guarded), so this does not recurse.
    printings_->setCurrentItem(nullptr);
    Q_EMIT selectionCleared();
}

void CardFinderPanel::clearSelection() { clearPreview(); }

void CardFinderPanel::searchFor(const QString& query) {
    if (nameMode_) {
        // Mirror what a user keystroke would do: put the text in the field, then run
        // the same gate onSearchTextChanged applies (3+ chars → search, else hint).
        searchField_->setText(query);
        onSearchTextChanged(query);
        return;
    }
    const QString wanted = query.trimmed();
    prefillNote_.clear();
    if (wanted.isEmpty()) {
        updateStatus();
        return;
    }
    if (search_.sets().empty()) {
        // Nothing to resolve against yet (a cold cache, or a load still in flight).
        // Hold the query rather than dropping it; onSetsReady() finishes the job.
        pendingSetQuery_ = query;
        updateStatus();
        return;
    }
    pendingSetQuery_.clear();
    // The host handed us a set CODE or NAME (a scanner reading, the last card's set), so
    // this is the one place a fuzzy filter still has to be resolved — and it is resolved
    // locally against the table, spending no request either way.
    const std::vector<std::string> ids = resolveSetFilterToIds(wanted.toStdString(),
                                                               search_.sets());
    if (ids.size() != 1) {
        // Never guess: narrowing to the wrong set would autofill the wrong printing.
        prefillNote_ = ids.empty()
                           ? tr("Couldn’t match “%1” to a set — pick it from the list.").arg(wanted)
                           : tr("“%1” matches several sets — pick the right one from the list.")
                                 .arg(wanted);
        updateStatus();
        return;
    }
    chooseSet(QString::fromStdString(ids.front()), /*emitChosen=*/false);
}

void CardFinderPanel::chooseSet(const QString& setId, bool emitChosen) {
    const CardSetInfo* set = findSetById(search_.sets(), setId);
    if (set == nullptr) {
        return;  // an id the table doesn't list; nothing to show and nothing to narrow by
    }
    prefillNote_.clear();
    selectSetEntry(setId);
    if (emitChosen) {
        Q_EMIT setChosen(*set);  // a host may autofill its own set fields from the pick
    }
    // These printings may already be on screen — re-picking the set the list is showing
    // is easy to do, and re-fetching it is pure cost: a request against an API that
    // fails plenty of them, and a failure would clear the list and the user's pick with
    // it. A failed or still-running search is NOT "already showing", so retrying by
    // re-picking the set works.
    if (setId == shownSetId_ && !loading_ && !failed_ && !candidates_.empty()) {
        updateStatus();
        return;
    }
    searchWith(setId);
}

bool CardFinderPanel::selectSetEntry(const QString& setId) {
    if (setCombo_ == nullptr) {
        return false;
    }
    const int index = setCombo_->findData(setId);
    if (index < 0) {
        return false;
    }
    // Programmatic: setCurrentIndex does not emit activated(), so this can't be mistaken
    // for a user pick (and can't recurse back into chooseSet).
    setCombo_->setCurrentIndex(index);
    return true;
}

bool CardFinderPanel::setTableMissing() const { return !nameMode_ && search_.sets().empty(); }

void CardFinderPanel::renderPreview() {
    // Called on selection changes and on resize, so it rescales whatever is showing.
    if (!previewPixmap_.isNull()) {
        setScaledPixmap(preview_, previewPixmap_);  // the picked card's image
        return;
    }
    if (selectedIndex_ >= 0) {
        // A card is picked but its large image is still loading or unavailable — leave
        // the text showPreview() set ("Loading card…" / "No image available…") intact.
        return;
    }
    // Nothing picked: show the host's placeholder (the copy's current image) if any,
    // else the hint.
    if (!placeholderPixmap_.isNull()) {
        setScaledPixmap(preview_, placeholderPixmap_);
    } else {
        preview_->setText(tr("Select a card to preview it."));
    }
}

void CardFinderPanel::setPreviewPlaceholder(const QPixmap& pixmap) {
    placeholderPixmap_ = pixmap;
    if (selectedIndex_ < 0) {
        renderPreview();  // nothing picked → reflect the new placeholder now
    }
}

void CardFinderPanel::searchWith(const QString& query) {
    loading_ = true;
    failed_ = false;
    lastQuery_ = query;  // what Retry re-runs, verbatim
    if (!nameMode_) {
        shownSetId_ = query;  // species mode: the query IS the set id being listed
    }
    updateStatus();
    // In name mode the field text is the card-name query; in species mode it is the
    // exact id of the picked set. The service tags the reply so onPrintingsReady still
    // matches by request id either way.
    pendingRequestId_ = nameMode_ ? search_.searchByName(query)
                                  : search_.searchPrintings(dexNumber_, query);
}

void CardFinderPanel::rebuildSetCombo() {
    // Fill the dropdown with one "CODE — Name" (or "Name") entry per set, each carrying
    // its set id as data — the id is what searches, so the label is display only.
    struct Entry {
        QString label;
        QString id;
    };
    std::vector<Entry> entries;
    entries.reserve(search_.sets().size());
    for (const CardSetInfo& s : search_.sets()) {
        if (s.name.empty() && s.ptcgoCode.empty()) {
            continue;  // nothing to show for it, and nothing the user could recognize
        }
        entries.push_back(Entry{setEntryLabel(s), QString::fromStdString(s.id)});
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return QString::compare(a.label, b.label, Qt::CaseInsensitive) < 0;
    });

    const QString keep = setCombo_->currentIndex() >= 0 ? setCombo_->currentData().toString()
                                                        : QString();
    // Repopulating must never read as a user pick: clear() alone would fire activated
    // in some styles, and setCurrentIndex is silent only for the index, not the model.
    const QSignalBlocker block(setCombo_);
    setCombo_->clear();
    for (const Entry& entry : entries) {
        setCombo_->addItem(entry.label, entry.id);
    }
    setCombo_->setCurrentIndex(-1);
    setCombo_->lineEdit()->clear();
    setCombo_->setEnabled(!entries.empty());

    // Entries can be long and differ only by a trailing year (McDonald's Collection
    // 2019/2020/…); size the popup to the longest one (capped) and never elide.
    const QFontMetrics fm(setCombo_->font());
    int widest = 0;
    for (const Entry& entry : entries) {
        widest = std::max(widest, fm.horizontalAdvance(entry.label));
    }
    const int popupWidth = widest > 0 ? std::min(widest + 32, 460) : 0;
    if (popupWidth > 0) {
        setCombo_->view()->setTextElideMode(Qt::ElideNone);
        setCombo_->view()->setMinimumWidth(popupWidth);
        if (QCompleter* completer = setCombo_->completer()) {
            completer->popup()->setTextElideMode(Qt::ElideNone);
            completer->popup()->setMinimumWidth(popupWidth);
        }
    }

    if (!keep.isEmpty()) {
        selectSetEntry(keep);  // the table was refilled under a live selection
    }
}

void CardFinderPanel::onSetsReady() {
    if (nameMode_) {
        // Re-run the current name search now that the table is available: results carry
        // expansion codes read from it, so one that landed without it is stale.
        if (searchField_->text().trimmed().size() >= 3) {
            onSearchTextChanged(searchField_->text());
        }
        return;
    }
    rebuildSetCombo();
    if (!pendingSetQuery_.isEmpty()) {
        // A host's prefill arrived before the table; now it can be resolved. Note this
        // is the ONLY thing that searches here — an arriving set table must not re-fire
        // a search the user never asked for.
        const QString query = pendingSetQuery_;
        pendingSetQuery_.clear();
        searchFor(query);
        return;
    }
    updateStatus();
}

void CardFinderPanel::updateStatus() {
    // A failed request and an empty result look identical on screen — both leave the
    // list empty — but they mean opposite things, so they must never share a message.
    // Saying "no printings found" after the catalog refused to answer is a claim about
    // the set that we have no basis for, and it sends the user off to hand-fill a form
    // for a card the catalog knows perfectly well.
    // A third thing that is not an empty set: with no set table there is nothing to
    // pick from at all, so the finder can't even be used. Only reachable when the
    // set-list load failed with no cache to fall back on (it is warmed at startup and
    // cached across launches), which is why it says the catalog may be down — the one
    // state here that is about the CATALOG rather than about a card.
    const bool loadingSets = setTableMissing() && search_.setsLoading();
    const bool noSetTable = setTableMissing() && !loadingSets;
    // Species mode: no set picked yet (nothing has been searched, so nothing to say
    // about results). Name mode keeps its own too-short-to-search gate.
    const bool awaitingChoice = nameMode_ ? searchField_->text().trimmed().size() < 3
                                          : setCombo_->currentIndex() < 0;
    retryButton_->setVisible(!loading_ && (failed_ || noSetTable));
    if (loadingSets) {
        status_->setText(tr("Loading the list of sets…"));
    } else if (noSetTable) {
        status_->setText(tr("The list of sets couldn’t be loaded, so there is no set to pick "
                            "here — the card catalog may be having problems. Retry, or fill "
                            "the form by hand."));
    } else if (loading_) {
        status_->setText(tr("Searching…"));
    } else if (failed_) {
        status_->setText(tr("The card catalog didn’t answer — the request failed, so this "
                            "is not “nothing found”. It flakes often; retrying usually works."));
    } else if (!prefillNote_.isEmpty()) {
        // Why a host's prefill picked no set. Ranked above the result lines on purpose:
        // when a set was already selected, those describe the OLD set, and the note is
        // the only thing that says the prefill didn't take.
        status_->setText(prefillNote_);
    } else if (awaitingChoice) {
        status_->setText(nameMode_
                             ? tr("Find a card by name — type its name (3+ characters).")
                             : tr("Find %1's cards — pick the set this card comes from.")
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
