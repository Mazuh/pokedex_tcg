#include "gui/views/add_card_copy_page.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <optional>
#include <utility>

#include "core/app/binder_service.h"
#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_reference.h"
#include "gui/services/card_image_store.h"
#include "gui/views/back_button.h"
#include "gui/views/card_copy_form.h"
#include "gui/views/card_copy_splitter.h"
#include "gui/views/card_finder_panel.h"

namespace pokedex {

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

    // --- Form (left): the shared details pane, editable --------------------
    form_ = new CardCopyForm(this);
    form_->setMaximumWidth(560);
    form_->setReferenceEditable(true);
    // Unscoped: a free binder choice ("— None —"). Scoped: pre-filled + locked.
    form_->setupBinderPicker(binders.list(), lockedBinder_, /*enabled=*/!lockedBinder_);
    // A user edit that no longer matches the picked card drops the preview; the
    // required collector number gates submit.
    connect(form_, &CardCopyForm::referenceEdited, this, [this]() {
        checkUnmatch();
        updateSubmitEnabled();
    });

    submit_ = new QPushButton(tr("Add copy"), this);
    connect(submit_, &QPushButton::clicked, this, &AddCardCopyPage::submitCopy);
    form_->addAction(submit_);

    // --- Finder (right): the shared search + preview widget ----------------
    finder_ = new CardFinderPanel(search, dexNumber_, speciesName, this);
    // When a set has no printings, remind the user the form on the left still works.
    finder_->setNoResultsHint(
        tr("you can still fill the form by hand, or the catalog may be flaking (retry)."));
    // A picked card autofills the form's card reference; a picked set fills the set
    // fields (so a coded set keeps its code even when the copy is filed by set only).
    connect(finder_, &CardFinderPanel::cardSelected, this, &AddCardCopyPage::autofillFrom);
    connect(finder_, &CardFinderPanel::setChosen, this, &AddCardCopyPage::chooseSet);

    // --- Assemble -----------------------------------------------------------
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(makeCardCopySplitter(form_, finder_));

    updateSubmitEnabled();
}

void AddCardCopyPage::autofillFrom(const CardCandidate& candidate) {
    form_->setCardReference(candidate.cardRef);
    updateSubmitEnabled();  // the autofilled collector number may now satisfy submit
}

void AddCardCopyPage::chooseSet(const CardSetInfo& set) {
    // Fill BOTH stored set fields from one chosen set (so a coded set keeps its code
    // even when picked by name), preserving whatever collector number is present.
    CardReference ref = form_->cardReference();
    ref.expansionCode = set.ptcgoCode;
    ref.setName = set.name;
    form_->setCardReference(ref);
}

void AddCardCopyPage::checkUnmatch() {
    if (!finder_->hasSelection()) {
        return;
    }
    // Once the user edits the reference away from the selected card, the preview no
    // longer represents the form — drop it. (Language/condition/etc. aren't part of
    // the printed identity, so they don't count.) Trim BOTH sides: the form fields are
    // trimmed, and a candidate ref parsed from the API may carry stray whitespace.
    const CardReference form = form_->cardReference();
    const CardReference ref = finder_->selectedCandidate().cardRef;
    const bool matches =
        QString::fromStdString(form.expansionCode).trimmed() ==
            QString::fromStdString(ref.expansionCode).trimmed() &&
        QString::fromStdString(form.setName).trimmed() ==
            QString::fromStdString(ref.setName).trimmed() &&
        QString::fromStdString(form.collectorNumber).trimmed() ==
            QString::fromStdString(ref.collectorNumber).trimmed();
    if (!matches) {
        finder_->clearSelection();
    }
}

void AddCardCopyPage::updateSubmitEnabled() {
    submit_->setEnabled(!form_->cardReference().collectorNumber.empty());
}

void AddCardCopyPage::submitCopy() {
    // When scoped, the locked binder is authoritative — the disabled combo is only a
    // display, so filing off lockedBinder_ can't silently land the copy unfiled if
    // that binder is missing from the combo. Unscoped, the user's combo choice wins.
    const std::optional<CardBinderId> binderId =
        lockedBinder_ ? lockedBinder_ : form_->binderId();
    CardCopy created;
    try {
        created = copies_.create(dexNumber_, form_->cardReference(), form_->ownership(),
                                 form_->condition(), binderId, form_->comments());
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
