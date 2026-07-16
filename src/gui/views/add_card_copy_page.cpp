#include "gui/views/add_card_copy_page.h"

#include <QComboBox>
#include <QCompleter>
#include <QEvent>
#include <QFont>
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

    expansionCode_ = new QLineEdit(formPane);
    expansionCode_->setPlaceholderText(tr("e.g. OBF"));
    // Editing either the code or the set name narrows the list (by code OR set-name
    // substring). Only USER edits narrow — programmatic autofill uses setText(),
    // which fires textChanged but NOT textEdited, so there is no feedback loop.
    connect(expansionCode_, &QLineEdit::textEdited, this,
            [this](const QString& text) { searchWith(text); });

    setName_ = new QLineEdit(formPane);
    setName_->setPlaceholderText(tr("e.g. Obsidian Flames or McDonald's"));
    connect(setName_, &QLineEdit::textEdited, this,
            [this](const QString& text) { searchWith(text); });

    language_ = new QComboBox(formPane);
    language_->addItems(languageCodes());

    collectorNumber_ = new QLineEdit(formPane);
    collectorNumber_->setPlaceholderText(tr("e.g. 125/197"));
    // The collector number is the required printed identity — it gates submit.
    connect(collectorNumber_, &QLineEdit::textChanged, this,
            [this](const QString&) { updateSubmitEnabled(); });

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

    // --- Printings list (right) --------------------------------------------
    auto* listPane = new QWidget(this);
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
    listLayout->addWidget(status_);
    listLayout->addWidget(printings_);

    // --- Assemble -----------------------------------------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(formPane);
    splitter->addWidget(listPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({420, 380});
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
    rebuildSetCompleter();  // sets may already be cached from a prior open

    updateStatus();
    updateSubmitEnabled();
    pendingRequestId_ = search_.searchPrintings(dexNumber_, QString());
}

bool AddCardCopyPage::eventFilter(QObject* watched, QEvent* event) {
    if (watched == printings_->viewport() && event->type() == QEvent::Resize) {
        fillViewport();
    }
    return QWidget::eventFilter(watched, event);
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
    if (QListWidgetItem* item = itemById_.value(cardId, nullptr)) {
        item->setIcon(QIcon(pixmap));
    }
}

void AddCardCopyPage::selectCandidate(int index) {
    if (index < 0 || index >= static_cast<int>(candidates_.size())) {
        return;
    }
    const CardCandidate& c = candidates_[index];
    // Autofill the printed identity. setText() does not fire textEdited, so this
    // does not re-narrow the search. Language / condition / ownership are the
    // user's to choose — the card source cannot supply them.
    expansionCode_->setText(QString::fromStdString(c.cardRef.expansionCode));
    setName_->setText(QString::fromStdString(c.cardRef.setName));
    collectorNumber_->setText(QString::fromStdString(c.cardRef.collectorNumber));
}

void AddCardCopyPage::searchWith(const QString& filter) {
    loading_ = true;
    updateStatus();
    pendingRequestId_ = search_.searchPrintings(dexNumber_, filter);
}

void AddCardCopyPage::rebuildSetCompleter() {
    // A set-name completer on the Set field, over EVERY set (code-less sets have a
    // name but no code, so a code-based picker would miss them). Picking a name
    // fills it and narrows the printings to that set.
    QStringList names;
    for (const CardSetInfo& s : search_.sets()) {
        if (!s.name.empty()) {
            names << QString::fromStdString(s.name);
        }
    }
    names.removeDuplicates();
    names.sort(Qt::CaseInsensitive);

    auto* completer = new QCompleter(names, setName_);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    connect(completer, qOverload<const QString&>(&QCompleter::activated), this,
            [this](const QString& picked) {
                setName_->setText(picked);
                searchWith(picked);  // an explicit pick should narrow the list
            });
    setName_->setCompleter(completer);
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
    // fields but keep the species, the printings list, and the sticky
    // condition/ownership choices.
    expansionCode_->clear();
    setName_->clear();
    collectorNumber_->clear();
    comments_->clear();
    status_->setText(tr("Added ✓ — add another, or go Back."));
}

void AddCardCopyPage::updateStatus() {
    if (loading_) {
        status_->setText(tr("Searching for %1 cards…").arg(speciesName_));
    } else if (candidates_.empty()) {
        status_->setText(
            tr("No cards to show — the catalog may be unreachable. You can still fill "
               "the form by hand."));
    } else {
        status_->setText(tr("Showing %1 of %2 printings — select one to autofill.")
                             .arg(loadedCount_)
                             .arg(static_cast<int>(candidates_.size())));
    }
}

}  // namespace pokedex
