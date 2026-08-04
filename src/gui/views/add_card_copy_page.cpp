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
#include <string>
#include <utility>

#include "core/app/binder_service.h"
#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "core/domain/card_reference.h"
#include "core/storage/workspace.h"
#include "gui/services/card_image_store.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/views/back_button.h"
#include "gui/views/card_copy_form.h"
#include "gui/views/card_copy_splitter.h"
#include "gui/views/card_finder_panel.h"
#include "gui/views/card_price_fetch_controller.h"
#include "gui/views/language_codes.h"
#include "gui/views/photo_upload.h"
#include "gui/views/primary_button.h"
#include "gui/views/rarity_from_catalog.h"
#include "gui/views/scaled_pixmap.h"
#include "gui/views/toast.h"

namespace pokedex {

// Set once per successful add, read by the next page's "Same set as last…" button.
AddCardCopyPage::LastAdded AddCardCopyPage::lastAdded_;

AddCardCopyPage::AddCardCopyPage(CardSearchService& search, CardCopyService& copies,
                                 CardPriceLookupService& priceLookup, BinderService& binders,
                                 CardImageStore& cardImages,
                                 std::optional<PokemonDexNum> dexNumber, const QString& speciesName,
                                 std::optional<CardBinderId> lockedBinder, QWidget* parent)
    : QWidget(parent),
      copies_(copies),
      priceLookup_(priceLookup),
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
    // Pre-select the user's default card language (Settings) on a fresh add, read live
    // from the config file so a change takes effect on the next add with no restart. A
    // reuse-last carry-over or a manual pick still overrides it. Read here (the page is
    // built fresh per open) rather than threaded through every host.
    if (const std::optional<std::string> defaultLang =
            readConfigValue(kDefaultLanguageConfigKey)) {
        form_->setLanguage(*defaultLang);
    }
    // A user edit that no longer matches the picked card drops the preview; the
    // required collector number gates submit.
    connect(form_, &CardCopyForm::referenceEdited, this, [this]() {
        checkUnmatch();
        updateSubmitEnabled();
    });

    submit_ = new QPushButton(tr("Add copy"), this);
    applyPrimaryButtonStyle(submit_);  // the primary/commit action — accent + ✓
    connect(submit_, &QPushButton::clicked, this, &AddCardCopyPage::submitCopy);
    form_->addAction(submit_);

    // Attach a photo of the card in hand instead of a catalog image. Held in memory
    // and only written at submit (see uploadPhoto) — unlike the Edit page's immediate
    // write, since a new copy has no id to key the image by until it is created.
    uploadButton_ = new QPushButton(tr("Upload a photo…"), this);
    connect(uploadButton_, &QPushButton::clicked, this, &AddCardCopyPage::uploadPhoto);
    form_->addAction(uploadButton_);

    // Prefill from the last copy added this session — the common case of entering
    // several cards from one booster, which share a set and often a note/language.
    // Naming the last card makes the button self-explanatory. Disabled until there is a
    // last add to reuse (the static memory survives this page being disposed on Back).
    const QString reuseLabel =
        lastAdded_.has && !lastAdded_.displayName.isEmpty()
            ? tr("Reuse last info from “%1”").arg(lastAdded_.displayName)
            : tr("Reuse last card’s info");
    reuseButton_ = new QPushButton(reuseLabel, this);
    reuseButton_->setToolTip(
        tr("Search this card's set and carry over the comments, language, and condition "
           "from the last card you added — handy when entering a whole booster."));
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

void AddCardCopyPage::prefillFrom(const QString& cardName, const QString& setName,
                                 const QString& setCode, const QString& collectorNumber) {
    // Write the read-off identity onto the form first, as a baseline the search can't lose:
    // even if the finder flakes or lists nothing, the set + number still save (and pricing
    // resolves from them). setCardReference is silent (no referenceEdited), so it doesn't
    // trip checkUnmatch.
    CardReference ref;
    ref.name = cardName.toStdString();
    ref.setName = setName.toStdString();
    ref.expansionCode = setCode.toStdString();
    ref.collectorNumber = collectorNumber.toStdString();
    form_->setCardReference(ref);

    // Then drive the finder (name-search mode here) so the catalog lists this card's
    // printings and a pick autofills the full identity + image. The card name is the
    // name-mode query; fall back to the set when there's no name to search on.
    const QString query = !cardName.isEmpty() ? cardName : setName;
    if (!query.isEmpty()) {
        finder_->searchFor(query);
    }
    updateSubmitEnabled();  // the prefilled collector number may already satisfy submit
}

void AddCardCopyPage::prefillSetSearch(const QString& setQuery) {
    // Species-scoped add from a scan: drive the finder's set search only, leaving the form
    // blank so the user picks the printing (which autofills the identity + image). Nothing
    // is written to the form — the picked card is the deterministic source of truth.
    if (!setQuery.trimmed().isEmpty()) {
        finder_->searchFor(setQuery);
    }
}

void AddCardCopyPage::reuseLastFields() {
    if (!lastAdded_.has) {
        return;  // button is disabled in this case, but guard anyway
    }
    // Carry over only the booster-shared bits the card search itself cannot supply: the
    // comment (into an EMPTY box only — never clobber a note already typed, nor blank it
    // when the last add had none), the language, and the condition.
    if (form_->comments().empty()) {
        form_->setComments(lastAdded_.comments);
    }
    form_->setLanguage(lastAdded_.language);
    form_->setCondition(lastAdded_.condition);

    // Reset the per-card printed identity to just the reused set: this is a NEW card from
    // the same set, so its name/collector number are entered fresh. Writing the set onto
    // the FORM (not only the finder search) matters two ways: (a) the set is never lost —
    // if the finder search flakes or lists nothing and the user completes the card by
    // hand, the set still saves, and pricing can resolve from it; (b) clearing the old
    // name/collector means a previously picked card can't be saved by mistake (submit
    // re-gates on the now-empty collector number). setCardReference is silent.
    CardReference ref;
    ref.expansionCode = lastAdded_.expansionCode;
    ref.setName = lastAdded_.setName;
    form_->setCardReference(ref);

    // In species mode also drive the finder search from the set, so the card search does
    // its natural job on top of the baseline above: list that set's printings and, once
    // the user picks the card, autofill the full identity (name, collector number,
    // rarity, image). Prefer the set name (how the finder's completer searches), falling
    // back to the expansion code.
    const QString setQuery = !lastAdded_.setName.empty()
                                 ? QString::fromStdString(lastAdded_.setName)
                                 : QString::fromStdString(lastAdded_.expansionCode);
    if (dexNumber_ && !setQuery.isEmpty()) {
        finder_->searchFor(setQuery);
    }
    // Drop any finder pick that no longer matches the reset form (covers name mode and
    // the no-set case, where no fresh search ran to clear it; a driven search clears it
    // on its own via onPrintingsReady).
    checkUnmatch();
    updateSubmitEnabled();
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
    // A new copy is created UNLINKED (blank external_card_id). Pricing is keyed by a
    // tcgdex card id, not the finder's pokemontcg id — the two schemes differ ("sv3-125"
    // vs "sv03-125") — so storing the finder pick's id here would be a wrong key. The
    // prices panel resolves the tcgdex id invisibly from the copy's set+collector-number
    // (which the finder pick fills into the reference) on the first Fetch, then persists it.
    CardCopy created;
    try {
        created = copies_.create(dexNumber_, form_->cardReference(), form_->ownership(),
                                 form_->condition(), form_->rarity(), form_->foil(), binderId,
                                 form_->comments(), /*externalCardId=*/std::string());
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
    // Remember this copy's set, physical attributes, comments, and a display name so the
    // next add page (a fresh instance) can prefill in one click — the same-booster flow.
    // The display name prefers the printed card name, falling back to the species name.
    QString reuseName = QString::fromStdString(created.cardRef.name);
    if (reuseName.isEmpty()) {
        reuseName = speciesName_;
    }
    lastAdded_ = LastAdded{/*has=*/true,        created.cardRef.expansionCode,
                           created.cardRef.setName, created.cardRef.language,
                           created.condition,    created.comments,
                           reuseName};

    // Kick off a best-effort background price fetch for the new copy, so the user no
    // longer has to press Fetch by hand after every add. The controller does the same
    // invisible resolve → link → fetch a Fetch button drives, but it is parented to the
    // app-wide price service (NOT this page, which the host disposes on the Back below),
    // so the work completes even after the page is gone: it persists the tcgdex link (the
    // host picks it up on its copyAdded reload) and, on completion, emits an app-wide
    // pricesReady that fills in the Prices column. It self-deletes when settled. Started
    // BEFORE copyAdded so a warm set table links the copy before the host reloads.
    auto* autoFetch = new CardPriceFetchController(priceLookup_, copies_, &priceLookup_);
    autoFetch->setCopy(created);
    if (autoFetch->canFetch()) {
        // Relay the resolved link app-wide so the host — which reloaded on copyAdded, possibly
        // before a cold set table finished resolving — writes the id into its cached copy and the
        // follow-up pricesReady fills the Prices column instead of being dropped by its guard.
        connect(autoFetch, &CardPriceFetchController::cardLinked, &priceLookup_,
                &CardPriceLookupService::copyAutoLinked);
        // Settle either way (fetch done, or a failure with no user to tell) → self-delete.
        connect(autoFetch, &CardPriceFetchController::pricesChanged, autoFetch,
                &QObject::deleteLater);
        connect(autoFetch, &CardPriceFetchController::fetchFailed, autoFetch,
                &QObject::deleteLater);
        autoFetch->autoFetch();  // honours the price TTL (a booster's re-adds don't re-fetch)
    } else {
        // Nothing to price (a copy with no set/number, e.g. a bare Trainer entry) — don't
        // leave the unused controller parented to the app-wide service.
        autoFetch->deleteLater();
    }

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
