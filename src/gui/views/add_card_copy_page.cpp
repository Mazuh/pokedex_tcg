#include "gui/views/add_card_copy_page.h"

#include <QComboBox>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStringList>
#include <QVBoxLayout>

#include <exception>
#include <optional>
#include <utility>

#include "core/app/binder_service.h"
#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_condition.h"
#include "core/domain/card_ownership.h"
#include "core/domain/card_reference.h"
#include "gui/services/card_image_store.h"
#include "gui/views/back_button.h"
#include "gui/views/binder_combo.h"
#include "gui/views/card_finder_panel.h"
#include "gui/views/splitter_style.h"

namespace pokedex {

namespace {

// The card languages this project recognizes (English-only source can't fill
// this, so it is always the user's choice). Leading blank = "unspecified".
const QStringList& languageCodes() {
    static const QStringList codes = {"", "EN", "FR", "DE", "IT", "ES",
                                      "LA", "PT", "C",  "F",  "T",  "I"};
    return codes;
}

}  // namespace

AddCardCopyPage::AddCardCopyPage(CardSearchService& search, CardCopyService& copies,
                                 BinderService& binders, CardImageStore& cardImages,
                                 int dexNumber, const QString& speciesName,
                                 std::optional<CardBinderId> lockedBinder, QWidget* parent)
    : QWidget(parent),
      copies_(copies),
      cardImages_(cardImages),
      dexNumber_(dexNumber),
      lockedBinder_(std::move(lockedBinder)) {
    // --- Top bar: Back + heading -------------------------------------------
    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &AddCardCopyPage::backRequested);

    auto* heading = new QLabel(tr("Add a copy — %1").arg(speciesName), this);
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
    // Condition is optional (a copy can be recorded ungraded) — default to blank, so
    // nothing is claimed unless the user picks a grade. The -1 sentinel means "none".
    condition_->addItem(tr("(Unspecified)"), -1);
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

    // The binder the copy is filed in. Opened unscoped, it is a free choice
    // defaulting to "— None —". Opened from within a binder (lockedBinder set), it is
    // pre-filled with that binder and disabled, so the copy lands where the user is.
    binder_ = new QComboBox(formPane);
    fillBinderCombo(*binder_, binders.list(), lockedBinder_);
    if (lockedBinder_) {
        binder_->setEnabled(false);
    }

    comments_ = new QPlainTextEdit(formPane);
    comments_->setPlaceholderText(
        tr("Capture story, price, seller, imperfections, dates…"));

    form->addRow(tr("Expansion code"), expansionCode_);
    form->addRow(tr("Set name"), setName_);
    form->addRow(tr("Language"), language_);
    form->addRow(tr("Collector number"), collectorNumber_);
    form->addRow(tr("Condition"), condition_);
    form->addRow(tr("Ownership"), ownership_);
    form->addRow(tr("Binder"), binder_);
    form->addRow(tr("Comments"), comments_);

    // Submit creates the copy; it stays disabled until the required collector
    // number is present.
    submit_ = new QPushButton(tr("Add copy"), formPane);
    connect(submit_, &QPushButton::clicked, this, &AddCardCopyPage::submitCopy);
    form->addRow(QString(), submit_);

    // --- Finder (right): the shared search + preview widget -----------------
    finder_ = new CardFinderPanel(search, dexNumber_, speciesName, this);
    // When a set has no printings, remind the user the form on the left still works.
    finder_->setNoResultsHint(
        tr("you can still fill the form by hand, or the catalog may be flaking (retry)."));
    // A picked card autofills the form's card reference; a picked set fills the set
    // fields (so a coded set keeps its code even when the copy is filed by set only).
    connect(finder_, &CardFinderPanel::cardSelected, this, &AddCardCopyPage::autofillFrom);
    connect(finder_, &CardFinderPanel::setChosen, this, &AddCardCopyPage::chooseSet);

    // --- Assemble -----------------------------------------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(formPane);
    splitter->addWidget(finder_);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 560});
    thinDivider(splitter);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(splitter);

    updateSubmitEnabled();
}

void AddCardCopyPage::autofillFrom(const CardCandidate& candidate) {
    // Autofill the printed identity from the picked card. setText() does not fire
    // textEdited, so this does not trip checkUnmatch. Language / condition / ownership
    // are the user's to choose — the card source cannot supply them.
    expansionCode_->setText(QString::fromStdString(candidate.cardRef.expansionCode));
    setName_->setText(QString::fromStdString(candidate.cardRef.setName));
    collectorNumber_->setText(QString::fromStdString(candidate.cardRef.collectorNumber));
}

void AddCardCopyPage::chooseSet(const CardSetInfo& set) {
    // Fill BOTH stored form fields from one chosen set (so a coded set keeps its code
    // even when picked by name). setText() does not fire textEdited, so this does not
    // trip checkUnmatch. The finder drives the search itself.
    expansionCode_->setText(QString::fromStdString(set.ptcgoCode));
    setName_->setText(QString::fromStdString(set.name));
}

void AddCardCopyPage::checkUnmatch() {
    if (!finder_->hasSelection()) {
        return;
    }
    // Once the user edits the reference away from the selected card, the preview no
    // longer represents the form — drop it. (Language/condition/etc. aren't part of
    // the printed identity, so they don't count.) Trim BOTH sides: the form fields are
    // trimmed, and a candidate ref parsed from the API may carry stray whitespace —
    // comparing trimmed-vs-raw would clear the preview spuriously the moment the user
    // touches any field.
    const CardCandidate selected = finder_->selectedCandidate();
    const CardReference& ref = selected.cardRef;
    const bool matches =
        expansionCode_->text().trimmed() == QString::fromStdString(ref.expansionCode).trimmed() &&
        setName_->text().trimmed() == QString::fromStdString(ref.setName).trimmed() &&
        collectorNumber_->text().trimmed() ==
            QString::fromStdString(ref.collectorNumber).trimmed();
    if (!matches) {
        finder_->clearSelection();
    }
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
    const int conditionData = condition_->currentData().toInt();
    const std::optional<CardCondition> condition =
        conditionData < 0 ? std::nullopt
                          : std::optional<CardCondition>(static_cast<CardCondition>(conditionData));
    // When scoped, the locked binder is authoritative — the disabled combo is only
    // a display, so filing off lockedBinder_ can't silently land the copy unfiled if
    // that binder is missing from the combo. Unscoped, the user's combo choice wins.
    const std::optional<CardBinderId> binderId =
        lockedBinder_ ? lockedBinder_ : binderComboSelection(*binder_);
    CardCopy created;
    try {
        created = copies_.create(dexNumber_, ref, ownership, condition, binderId,
                                 comments_->toPlainText().toStdString());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not add the copy:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }
    // Persist the picked card's image for "My Cards" to show. A finder selection means
    // a real printing is picked (checkUnmatch drops it once the form is edited
    // off-card). If its preview already loaded, save that pixmap outright (no
    // re-download); otherwise the user submitted before it finished, so fetch it by
    // URL — the store outlives this page, so the download still lands. Both are
    // best-effort: a failure never blocks the copy.
    if (finder_->hasSelection()) {
        const QPixmap preview = finder_->selectedPreview();
        if (!preview.isNull()) {
            cardImages_.save(created.id, preview);
        } else {
            const CardCandidate c = finder_->selectedCandidate();
            const QString url = QString::fromStdString(
                c.imageUrlLarge.empty() ? c.imageUrlSmall : c.imageUrlLarge);
            cardImages_.fetchAndSave(created.id, url);  // no-ops on a blank url
        }
    }
    Q_EMIT copyAdded();
    // Return to the previous screen after a successful add — the host's
    // backRequested handler pops this page. (Emit last: the handler schedules the
    // page for deletion via deleteLater, so no member is touched afterward.)
    Q_EMIT backRequested();
}

}  // namespace pokedex
