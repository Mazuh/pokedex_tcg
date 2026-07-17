#include "gui/views/add_card_copy_page.h"

#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStringList>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>
#include <optional>

#include "core/app/card_catalog_parse.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_condition.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "gui/services/card_search_service.h"
#include "gui/views/splitter_style.h"

namespace pokedex {

namespace {

// Chunk + prefetch tuning mirrors PokemonListView's infinite scroll.
constexpr int kChunkSize = 20;
constexpr int kPrefetchMargin = 64;

// The card thumbnails are portrait; size the row icon to that aspect.
constexpr int kThumbW = 48;
constexpr int kThumbH = 66;

// The card languages this project recognizes (English-only source can't fill
// this, so it is always the user's choice). Leading blank = "unspecified".
const QStringList& languageCodes() {
    static const QStringList codes = {"", "EN", "FR", "DE", "IT", "ES",
                                      "LA", "PT", "C",  "F",  "T",  "I"};
    return codes;
}

}  // namespace

AddCardCopyPage::AddCardCopyPage(CardSearchService& search, CardCopyService& copies,
                                 int dexNumber, const QString& speciesName, QWidget* parent)
    : QWidget(parent),
      search_(search),
      copies_(copies),
      dexNumber_(dexNumber),
      speciesName_(speciesName) {
    // --- Top bar: Back + heading -------------------------------------------
    auto* backButton = new QPushButton(tr("Back"), this);
    backButton->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    connect(backButton, &QPushButton::clicked, this, &AddCardCopyPage::backRequested);

    auto* heading = new QLabel(tr("Add a copy — %1").arg(speciesName_), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    // --- Form (left) --------------------------------------------------------
    auto* formPane = new QWidget(this);
    auto* form = new QFormLayout(formPane);
    // Let the inputs grow to fill the column (macOS defaults to leaving them at
    // their small size hint, which clips values like "McDonald's Collection 2021"),
    // and cap the pane so they don't become absurdly wide on a large window.
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    formPane->setMaximumWidth(560);

    // The reference fields are stored data (autofilled from a picked card, or typed).
    // A USER edit (textEdited, not the autofill's setText) that no longer matches the
    // selected card drops the preview via checkUnmatch().
    expansionCode_ = new QLineEdit(formPane);
    expansionCode_->setPlaceholderText(tr("e.g. OBF"));
    connect(expansionCode_, &QLineEdit::textEdited, this,
            [this](const QString&) { checkUnmatch(); });

    setName_ = new QLineEdit(formPane);
    setName_->setPlaceholderText(tr("e.g. Obsidian Flames"));
    connect(setName_, &QLineEdit::textEdited, this, [this](const QString&) { checkUnmatch(); });

    language_ = new QComboBox(formPane);
    language_->addItems(languageCodes());

    collectorNumber_ = new QLineEdit(formPane);
    collectorNumber_->setPlaceholderText(tr("e.g. 125/197"));
    // The collector number is the required printed identity — it gates submit.
    connect(collectorNumber_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateSubmitEnabled(); });
    connect(collectorNumber_, &QLineEdit::textEdited, this,
            [this](const QString&) { checkUnmatch(); });

    condition_ = new QComboBox(formPane);
    condition_->addItem(tr("Near Mint"), static_cast<int>(CardCondition::NearMint));
    condition_->addItem(tr("Lightly Played"), static_cast<int>(CardCondition::LightlyPlayed));
    condition_->addItem(tr("Moderately Played"), static_cast<int>(CardCondition::ModeratelyPlayed));
    condition_->addItem(tr("Heavily Played"), static_cast<int>(CardCondition::HeavilyPlayed));
    condition_->addItem(tr("Damaged"), static_cast<int>(CardCondition::Damaged));

    ownership_ = new QComboBox(formPane);
    ownership_->addItem(tr("Incoming"), static_cast<int>(CardOwnership::Incoming));
    ownership_->addItem(tr("Owned"), static_cast<int>(CardOwnership::Owned));
    ownership_->addItem(tr("Removed"), static_cast<int>(CardOwnership::Removed));
    ownership_->setCurrentIndex(ownership_->findData(static_cast<int>(CardOwnership::Owned)));

    comments_ = new QPlainTextEdit(formPane);
    comments_->setPlaceholderText(
        tr("Capture story, price, seller, imperfections, dates…"));

    form->addRow(tr("Expansion code"), expansionCode_);
    form->addRow(tr("Set"), setName_);
    form->addRow(tr("Language"), language_);
    form->addRow(tr("Collector number"), collectorNumber_);
    form->addRow(tr("Condition"), condition_);
    form->addRow(tr("Ownership"), ownership_);
    form->addRow(tr("Comments"), comments_);

    // Submit creates the copy; it stays disabled until the required collector
    // number is present.
    submit_ = new QPushButton(tr("Add copy"), formPane);
    connect(submit_, &QPushButton::clicked, this, &AddCardCopyPage::submitCopy);
    form->addRow(QString(), submit_);

    // --- Finder (middle): search field + results ---------------------------
    auto* finderPane = new QWidget(this);
    searchField_ = new QLineEdit(finderPane);
    searchField_->setPlaceholderText(tr("Find by set — code or name, 3+ characters…"));
    searchField_->setClearButtonEnabled(true);
    connect(searchField_, &QLineEdit::textEdited, this, &AddCardCopyPage::onSearchTextChanged);

    status_ = new QLabel(finderPane);
    status_->setEnabled(false);  // muted status/hint text
    status_->setWordWrap(true);

    printings_ = new QListWidget(finderPane);
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

    auto* finderLayout = new QVBoxLayout(finderPane);
    finderLayout->setContentsMargins(0, 0, 0, 0);
    finderLayout->addWidget(searchField_);
    finderLayout->addWidget(status_);
    finderLayout->addWidget(printings_);

    // --- Preview (right): the selected card, larger ------------------------
    auto* previewPane = new QWidget(this);
    preview_ = new QLabel(previewPane);
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setMinimumWidth(180);
    preview_->setWordWrap(true);
    preview_->installEventFilter(this);  // rescale the image when the pane resizes
    auto* previewLayout = new QVBoxLayout(previewPane);
    previewLayout->setContentsMargins(0, 0, 0, 0);
    previewLayout->addWidget(preview_);

    // --- Assemble -----------------------------------------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(formPane);
    splitter->addWidget(finderPane);
    splitter->addWidget(previewPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setStretchFactor(2, 1);
    splitter->setSizes({320, 300, 260});
    thinDivider(splitter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(splitter);

    // --- Wire the search service -------------------------------------------
    connect(&search_, &CardSearchService::printingsReady, this,
            &AddCardCopyPage::onPrintingsReady);
    connect(&search_, &CardSearchService::printingsFailed, this,
            &AddCardCopyPage::onPrintingsFailed);
    connect(&search_, &CardSearchService::thumbnailReady, this,
            &AddCardCopyPage::onThumbnailReady);
    connect(&search_, &CardSearchService::setsReady, this,
            &AddCardCopyPage::rebuildSetCompleter);
    rebuildSetCompleter();  // the set table is warmed at startup, so usually ready

    // Nothing is fetched on open — a species can have hundreds of printings. The
    // user searches by set first (see onSearchTextChanged).
    clearPreview();
    updateStatus();
    updateSubmitEnabled();
}

bool AddCardCopyPage::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        if (watched == printings_->viewport()) {
            fillViewport();
        } else if (watched == preview_) {
            renderPreview();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void AddCardCopyPage::onSearchTextChanged(const QString& text) {
    const QString t = text.trimmed();
    if (t.size() < 3) {
        clearResults();  // too short to search
        updateStatus();  // falls back to the "type 3+ chars" hint
        return;
    }
    // Only fetch when the text actually matches a set; otherwise the query would
    // resolve to no set ids and fall back to fetching ALL of the species' printings.
    if (resolveSetFilterToIds(t.toStdString(), search_.sets()).empty()) {
        clearResults();
        status_->setText(
            tr("No set matches “%1” — try a code (OBF) or name (Obsidian Flames).").arg(t));
        return;
    }
    searchWith(text);  // the service debounces the actual request
}

void AddCardCopyPage::clearResults() {
    pendingRequestId_ = 0;  // ignore any in-flight reply for a now-abandoned query
    loading_ = false;
    candidates_.clear();
    loadedCount_ = 0;
    itemById_.clear();
    printings_->clear();
    clearPreview();
}

void AddCardCopyPage::onPrintingsReady(std::uint64_t requestId, int dexNumber,
                                       const std::vector<CardCandidate>& cards) {
    Q_UNUSED(dexNumber);
    if (requestId != pendingRequestId_) {
        return;  // not this page's latest search (another live page, or superseded)
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

void AddCardCopyPage::onPrintingsFailed(std::uint64_t requestId, int dexNumber) {
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

void AddCardCopyPage::loadMore() {
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

void AddCardCopyPage::fillViewport() {
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

void AddCardCopyPage::onThumbnailReady(const QString& cardId, const QPixmap& pixmap) {
    if (!previewCardId_.isEmpty() && cardId == previewCardId_) {
        previewPixmap_ = pixmap;  // the large image for the selected card
        renderPreview();
        return;
    }
    if (QListWidgetItem* item = itemById_.value(cardId, nullptr)) {
        item->setIcon(QIcon(pixmap));
    }
}

void AddCardCopyPage::selectCandidate(int index) {
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return;
    }
    selectedIndex_ = index;
    const CardCandidate& c = candidates_[index];
    // Autofill the printed identity. setText() does not fire textEdited, so this
    // does not trip checkUnmatch. Language / condition / ownership are the user's to
    // choose — the card source cannot supply them.
    expansionCode_->setText(QString::fromStdString(c.cardRef.expansionCode));
    setName_->setText(QString::fromStdString(c.cardRef.setName));
    collectorNumber_->setText(QString::fromStdString(c.cardRef.collectorNumber));
    showPreview(index);
}

void AddCardCopyPage::showPreview(int index) {
    const CardCandidate& c = candidates_[index];
    previewPixmap_ = QPixmap();
    // Keyed distinctly from the row thumbnail ("preview:" prefix) so both can be in
    // flight; fetched into memory only (never cached to disk), like every card image.
    previewCardId_ = QStringLiteral("preview:") + QString::fromStdString(c.id);
    preview_->setText(tr("Loading card…"));
    const QString url = QString::fromStdString(c.imageUrlLarge.empty() ? c.imageUrlSmall
                                                                       : c.imageUrlLarge);
    search_.fetchThumbnail(previewCardId_, url);
}

void AddCardCopyPage::clearPreview() {
    selectedIndex_ = -1;
    previewCardId_.clear();
    previewPixmap_ = QPixmap();
    preview_->setText(tr("Select a card to preview it."));
    // Drop the list highlight; setCurrentItem(nullptr) fires currentItemChanged with a
    // null current, which selectCandidate ignores (guarded), so this does not recurse.
    printings_->setCurrentItem(nullptr);
}

void AddCardCopyPage::renderPreview() {
    if (previewPixmap_.isNull()) {
        return;
    }
    const qreal dpr = devicePixelRatioF();
    QPixmap scaled = previewPixmap_.scaled(preview_->size() * dpr, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    preview_->setPixmap(scaled);
}

void AddCardCopyPage::checkUnmatch() {
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(candidates_.size())) {
        return;
    }
    // Once the user edits the reference away from the selected card, the preview no
    // longer represents the form — drop it. (Language/condition/etc. aren't part of
    // the printed identity, so they don't count.)
    const CardReference& ref = candidates_[selectedIndex_].cardRef;
    const bool matches =
        expansionCode_->text().trimmed().toStdString() == ref.expansionCode &&
        setName_->text().trimmed().toStdString() == ref.setName &&
        collectorNumber_->text().trimmed().toStdString() == ref.collectorNumber;
    if (!matches) {
        clearPreview();
    }
}

void AddCardCopyPage::searchWith(const QString& filter) {
    loading_ = true;
    updateStatus();
    pendingRequestId_ = search_.searchPrintings(dexNumber_, filter);
}

void AddCardCopyPage::chooseSet(const CardSetInfo& set) {
    // Fill BOTH stored form fields from one chosen set (so a coded set keeps its code
    // even when picked by name). setText() does not fire textEdited, so this does not
    // trip checkUnmatch. The caller drives the search.
    expansionCode_->setText(QString::fromStdString(set.ptcgoCode));
    setName_->setText(QString::fromStdString(set.name));
}

void AddCardCopyPage::rebuildSetCompleter() {
    // A "CODE — Name" (or just "Name" for code-less sets) typeahead on the finder's
    // search field. Picking one fills the form's set fields and fetches that set's
    // cards for this species.
    QStringList entries;
    for (const CardSetInfo& s : search_.sets()) {
        if (s.name.empty() && s.ptcgoCode.empty()) {
            continue;
        }
        const QString name = QString::fromStdString(s.name);
        const QString code = QString::fromStdString(s.ptcgoCode);
        entries << (code.isEmpty() ? name : QStringLiteral("%1 — %2").arg(code, name));
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
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this,
            [this](const QString& picked) {
                // Map the picked "CODE — Name"/"Name" entry back to its set, fill the
                // form, and fetch that set's cards for this species.
                for (const CardSetInfo& s : search_.sets()) {
                    const QString name = QString::fromStdString(s.name);
                    const QString code = QString::fromStdString(s.ptcgoCode);
                    const QString entry =
                        code.isEmpty() ? name : QStringLiteral("%1 — %2").arg(code, name);
                    if (entry == picked) {
                        chooseSet(s);
                        searchWith(name.isEmpty() ? code : name);
                        return;
                    }
                }
            });
    searchField_->setCompleter(completer);
}

void AddCardCopyPage::updateSubmitEnabled() {
    submit_->setEnabled(!collectorNumber_->text().trimmed().isEmpty());
}

void AddCardCopyPage::submitCopy() {
    CardReference ref;
    ref.expansionCode = expansionCode_->text().trimmed().toStdString();
    ref.language = language_->currentText().toStdString();  // "" when unspecified
    ref.collectorNumber = collectorNumber_->text().trimmed().toStdString();
    ref.setName = setName_->text().trimmed().toStdString();
    const auto ownership = static_cast<CardOwnership>(ownership_->currentData().toInt());
    const auto condition = static_cast<CardCondition>(condition_->currentData().toInt());
    try {
        // No binder for now — assigning a copy to a binder is a later concern.
        copies_.create(dexNumber_, ref, ownership, condition, std::nullopt,
                       comments_->toPlainText().toStdString());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not add the copy:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }
    Q_EMIT copyAdded();
    // Stay on the page so several copies can be added in a row: clear the entry
    // fields and the preview but keep the search + results and the sticky
    // condition/ownership choices.
    expansionCode_->clear();
    setName_->clear();
    collectorNumber_->clear();
    comments_->clear();
    clearPreview();
    status_->setText(tr("Added ✓ — add another, or go Back."));
}

void AddCardCopyPage::updateStatus() {
    if (loading_) {
        status_->setText(tr("Searching…"));
    } else if (searchField_->text().trimmed().size() < 3) {
        status_->setText(tr("Find %1's cards by set — type a code or name (3+ characters).")
                             .arg(speciesName_));
    } else if (candidates_.empty()) {
        status_->setText(tr("No printings found for that set — you can still fill the form "
                            "by hand, or the catalog may be flaking (retry)."));
    } else {
        status_->setText(tr("Showing %1 of %2 — select a card to autofill.")
                             .arg(loadedCount_)
                             .arg(static_cast<int>(candidates_.size())));
    }
}

}  // namespace pokedex
