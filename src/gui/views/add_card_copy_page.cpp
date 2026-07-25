#include "gui/views/add_card_copy_page.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QStackedWidget>
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
#include "gui/views/photo_upload.h"
#include "gui/views/rarity_from_catalog.h"
#include "gui/views/scaled_pixmap.h"
#include "gui/views/toast.h"

namespace pokedex {

// Set once per successful add, read by the next page's "Same set as last…" button.
AddCardCopyPage::LastAdded AddCardCopyPage::lastAdded_;

AddCardCopyPage::AddCardCopyPage(CardSearchService& search, CardCopyService& copies,
                                 BinderService& binders, CardImageStore& cardImages,
                                 std::optional<PokemonDexNum> dexNumber, const QString& speciesName,
                                 std::optional<CardBinderId> lockedBinder, QWidget* parent)
    : QWidget(parent),
      copies_(copies),
      cardImages_(cardImages),
      dexNumber_(dexNumber),
      speciesName_(speciesName),
      lockedBinder_(std::move(lockedBinder)) {
    // --- Top bar: Back + heading -------------------------------------------
    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &AddCardCopyPage::handleBack);

    // Species-scoped names the Pokémon; species-free (a Trainer/Energy card) can't.
    auto* heading = new QLabel(
        dexNumber_ ? tr("Add a copy — %1").arg(speciesName) : tr("Add a card"), this);
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

    // Attach a photo of the card in hand instead of a catalog image. Held in memory
    // and only written at submit (see uploadPhoto) — unlike the Edit page's immediate
    // write, since a new copy has no id to key the image by until it is created.
    uploadButton_ = new QPushButton(tr("Upload a photo…"), this);
    connect(uploadButton_, &QPushButton::clicked, this, &AddCardCopyPage::uploadPhoto);
    form_->addAction(uploadButton_);

    // Refill the set fields (expansion code + set name) and the comments from the last
    // copy added this session — the common case of entering several cards from one
    // booster, which share a set and often a note. Disabled until there is a last add
    // to reuse (the static memory survives this page being disposed on each Back).
    reuseButton_ = new QPushButton(tr("Same set as last…"), this);
    reuseButton_->setToolTip(
        tr("Copy the set name, expansion code, and comments from the last card you "
           "added — handy when entering a whole booster."));
    reuseButton_->setEnabled(lastAdded_.has);
    connect(reuseButton_, &QPushButton::clicked, this, &AddCardCopyPage::reuseLastFields);
    form_->addAction(reuseButton_);

    // --- Finder (right): the shared search + preview widget ----------------
    // Scoped: search the species' printings by set. Species-free: search by card name
    // (there is no dex number to scope by).
    finder_ = dexNumber_
                  ? new CardFinderPanel(search, *dexNumber_, speciesName, this)
                  : new CardFinderPanel(search, CardFinderPanel::NameSearchMode{}, QString(), this);
    // When a search has no printings, remind the user the form on the left still works.
    finder_->setNoResultsHint(
        tr("you can still fill the form by hand, or the catalog may be flaking (retry)."));
    // A picked card autofills the form's card reference; a picked set fills the set
    // fields (so a coded set keeps its code even when the copy is filed by set only).
    connect(finder_, &CardFinderPanel::cardSelected, this, &AddCardCopyPage::autofillFrom);
    connect(finder_, &CardFinderPanel::setChosen, this, &AddCardCopyPage::chooseSet);

    // --- Uploaded-photo page: shown in place of the finder once a photo is uploaded -
    auto* photoPage = new QWidget(this);
    auto* photoLayout = new QVBoxLayout(photoPage);
    auto* photoCaption = new QLabel(tr("Uploaded photo — saved when you add the copy"), photoPage);
    photoCaption->setEnabled(false);  // muted, like the Edit page's current-image label
    photoCaption->setAlignment(Qt::AlignHCenter);
    uploadedPreview_ = new QLabel(photoPage);
    uploadedPreview_->setMinimumSize(240, 320);
    uploadedPreview_->setAlignment(Qt::AlignCenter);
    uploadedPreview_->installEventFilter(this);  // rescale the photo when the pane resizes
    auto* removeButton = new QPushButton(tr("Remove photo"), photoPage);
    connect(removeButton, &QPushButton::clicked, this, &AddCardCopyPage::removeUploadedPhoto);
    photoLayout->addWidget(photoCaption, /*stretch=*/0);
    photoLayout->addWidget(uploadedPreview_, /*stretch=*/1);
    photoLayout->addWidget(removeButton, /*stretch=*/0, Qt::AlignHCenter);

    // --- Assemble -----------------------------------------------------------
    // The right side is a stack: the finder normally, the uploaded photo when set.
    rightStack_ = new QStackedWidget(this);
    rightStack_->addWidget(finder_);     // index 0
    rightStack_->addWidget(photoPage);   // index 1

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(makeCardCopySplitter(form_, rightStack_));

    updateSubmitEnabled();
}

void AddCardCopyPage::autofillFrom(const CardCandidate& candidate) {
    form_->setCardReference(candidate.cardRef);
    // Best-effort pre-fill of the rarity from the catalog's rarity string (blank when
    // it doesn't map to one of our rarities — the picker stays editable). Foil has no
    // catalog source, so it is always the user's pick.
    form_->setRarity(rarityFromCatalog(candidate.rarity));
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

void AddCardCopyPage::reuseLastFields() {
    if (!lastAdded_.has) {
        return;  // button is disabled in this case, but guard anyway
    }
    // Fill only the shared-across-a-booster fields: the set (expansion code + set name)
    // and the comments. The per-card identity (card name, collector number) and the
    // physical attributes stay untouched — each card in the pack differs there.
    CardReference ref = form_->cardReference();
    ref.expansionCode = lastAdded_.expansionCode;
    ref.setName = lastAdded_.setName;
    form_->setCardReference(ref);
    // Only carry the comment over into an empty box — never clobber a note the user has
    // already typed for this card (nor blank it out when the last add had no comment).
    if (form_->comments().empty()) {
        form_->setComments(lastAdded_.comments);
    }
    // setCardReference is silent, so drop a now-stale finder pick by hand (its preview
    // no longer matches the freshly filled set).
    checkUnmatch();
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
        QString::fromStdString(form.name).trimmed() ==
            QString::fromStdString(ref.name).trimmed() &&
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

void AddCardCopyPage::uploadPhoto() {
    // Unlike the Edit page (which writes the pixmap straight to the store), hold it in
    // memory: the new copy has no id to key the image by yet, and an abandoned add
    // should leave no file. It is persisted in submitCopy() once the copy exists.
    const std::optional<QPixmap> pixmap = pickCardPhoto(this);
    if (!pixmap) {
        return;  // cancelled, or unreadable (pickCardPhoto already warned)
    }
    uploadedImage_ = *pixmap;      // held, not saved
    refreshUploadedPreview();
    rightStack_->setCurrentIndex(1);  // replace the search with the uploaded photo
}

void AddCardCopyPage::removeUploadedPhoto() {
    uploadedImage_ = QPixmap();       // drop the held photo
    refreshUploadedPreview();         // clears the label (its single source of truth)
    rightStack_->setCurrentIndex(0);  // the finder returns with whatever state it had
}

void AddCardCopyPage::refreshUploadedPreview() {
    if (uploadedImage_.isNull()) {
        uploadedPreview_->clear();
        return;
    }
    setScaledPixmap(uploadedPreview_, uploadedImage_);
}

bool AddCardCopyPage::eventFilter(QObject* watched, QEvent* event) {
    // The held photo is scaled from its full-res original on every resize (matching
    // CardFinderPanel's preview), so it fills the pane instead of freezing at the size
    // it had when first uploaded — and the first render, once the page becomes current
    // and the label takes its real size, comes through here too.
    if (event->type() == QEvent::Resize && watched == uploadedPreview_) {
        refreshUploadedPreview();
    }
    return QWidget::eventFilter(watched, event);
}

bool AddCardCopyPage::isDirty() const {
    // "Dirty" = the user has entered something that would be lost. Every field starts
    // pristine (empty identity, unspecified language/condition, Owned, empty comments),
    // so any departure from that is worth confirming. A picked finder card is covered
    // by the identity check: it autofills those fields (and checkUnmatch drops the pick
    // once they're edited away), so it never needs a separate term here.
    const CardReference ref = form_->cardReference();
    if (!ref.name.empty() || !ref.expansionCode.empty() || !ref.setName.empty() ||
        !ref.collectorNumber.empty() || !ref.language.empty()) {
        return true;
    }
    if (!form_->comments().empty() || form_->condition().has_value() ||
        form_->rarity().has_value() || form_->foil().has_value() ||
        form_->ownership() != CardOwnership::Owned) {
        return true;
    }
    // An uploaded-but-unsaved photo would be lost on Back — worth confirming.
    if (!uploadedImage_.isNull()) {
        return true;
    }
    // The binder combo only counts when unscoped (scoped is pre-filled and locked, so
    // it isn't the user's doing).
    return !lockedBinder_ && form_->binderId().has_value();
}

void AddCardCopyPage::handleBack() {
    // Don't drop a partly-composed copy on Back without a word — offer to discard it or
    // stay. (A successful "Add copy" emits backRequested() directly, bypassing this,
    // since the form was consumed.) An explicit "Discard" button reads clearer than the
    // stock Discard role, which macOS labels "Don't Save" (there is no save here).
    if (isDirty()) {
        QMessageBox box(QMessageBox::Question, tr("Discard new card?"),
                        tr("You haven't added this card yet. Leave and discard it?"),
                        QMessageBox::NoButton, this);
        QPushButton* discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(QMessageBox::Cancel);  // safe default: stay
        box.exec();
        if (box.clickedButton() != discard) {
            return;  // Cancel (or closed) → stay on the page
        }
    }
    Q_EMIT backRequested();
}

void AddCardCopyPage::submitCopy() {
    // When scoped, the locked binder is authoritative — the disabled combo is only a
    // display, so filing off lockedBinder_ can't silently land the copy unfiled if
    // that binder is missing from the combo. Unscoped, the user's combo choice wins.
    const std::optional<CardBinderId> binderId =
        lockedBinder_ ? lockedBinder_ : form_->binderId();
    // Link the copy to its catalog card only when a real printing is still picked in
    // the finder (checkUnmatch drops the selection once the form is edited off-card),
    // so the id always matches the recorded reference — same guard the image uses.
    // Without a selection (hand-entered / photo-only) the copy is unlinked, and its
    // prices simply can't be looked up.
    const std::string externalCardId =
        finder_->hasSelection() ? finder_->selectedCandidate().id : std::string();
    CardCopy created;
    try {
        created = copies_.create(dexNumber_, form_->cardReference(), form_->ownership(),
                                 form_->condition(), form_->rarity(), form_->foil(), binderId,
                                 form_->comments(), externalCardId);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not add the copy:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }
    // Persist the copy's image for "My Cards" to show. An uploaded photo wins: it was
    // held in memory precisely so it could be written here, keyed by the new copy's id.
    // Otherwise fall back to the finder — a selection means a real printing is picked
    // (checkUnmatch drops it once the form is edited off-card). If its preview already
    // loaded, save that pixmap outright (no re-download); otherwise the user submitted
    // before it finished, so fetch it by URL — the store outlives this page, so the
    // download still lands. All best-effort: a failure never blocks the copy.
    if (!uploadedImage_.isNull()) {
        cardImages_.save(created.id, uploadedImage_);
    } else if (finder_->hasSelection()) {
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
    // Remember this copy's set + comments so the next add page (a fresh instance) can
    // refill them in one click — the same-booster flow. Kept in memory only.
    lastAdded_ = LastAdded{/*has=*/true, created.cardRef.expansionCode,
                           created.cardRef.setName, created.comments};

    // Confirm before navigating away: the toast is parented to the window, so it
    // outlives this page once backRequested() disposes of it. A species-free card has
    // no species to name — fall back to its card name, or a generic line.
    QString toastText;
    if (!speciesName_.isEmpty()) {
        toastText = tr("Copy of %1 added.").arg(speciesName_);
    } else if (const QString name = QString::fromStdString(created.cardRef.name);
               !name.isEmpty()) {
        toastText = tr("%1 added.").arg(name);
    } else {
        toastText = tr("Card added.");
    }
    showToast(this, toastText);
    Q_EMIT copyAdded();
    // Return to the previous screen after a successful add — the host's
    // backRequested handler pops this page. (Emit last: the handler schedules the
    // page for deletion via deleteLater, so no member is touched afterward.)
    Q_EMIT backRequested();
}

}  // namespace pokedex
