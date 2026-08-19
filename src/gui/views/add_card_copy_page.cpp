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
#include "core/app/card_scan.h"
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

// Set once per successful add, read by the next page's two booster shortcuts — the
// "Reuse comments from …" and "Search set …" buttons.
AddCardCopyPage::LastAdded AddCardCopyPage::lastAdded_;

namespace {

// How the last add's set is named, for both the "Search set …" button's label and the
// query it runs — so the button can never search something other than what it says.
// Prefers the set name (how the finder resolves a set), falling back to its code.
QString lastSetQuery(const std::string& setName, const std::string& expansionCode) {
    return QString::fromStdString(!setName.empty() ? setName : expansionCode);
}

}  // namespace

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
    // manual pick still overrides it (nothing else writes the language). Read here (the
    // page is built fresh per open) rather than threaded through every host.
    if (const std::optional<std::string> defaultLang =
            readConfigValue(kDefaultLanguageConfigKey)) {
        form_->setLanguage(*defaultLang);
        seededLanguage_ = *defaultLang;  // isDirty() must not read our own pre-select as input
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

    // Two narrow shortcuts off the last copy added this session — the common case of
    // entering several cards from one booster, which share a note and a set. Each does
    // exactly what its label says and nothing more; neither rewrites the printed
    // identity behind the user's back. Disabled until there is something to reuse (the
    // static memory survives this page being disposed on Back).
    //
    // The labels are SHORT and static, with the last card/set named in the tooltip
    // instead: the form's action row is one non-wrapping QHBoxLayout inside a pane
    // capped at 560px, so interpolating a card name and a full set name into two of its
    // four buttons overflows it and clips the trailing button — unclickable in exactly
    // the same-booster flow it exists for. Any further action here has the same budget.
    reuseCommentsButton_ = new QPushButton(tr("Reuse comments"), this);
    // An empty comment is nothing to carry over, so don't offer the click — and say why,
    // rather than leaving a greyed button unexplained.
    const bool canReuseComments = lastAdded_.has && !lastAdded_.comments.empty();
    reuseCommentsButton_->setEnabled(canReuseComments);
    reuseCommentsButton_->setToolTip(
        canReuseComments
            ? tr("Put the comments from the last card you added (“%1”) into this copy's "
                 "comments box, replacing whatever is there — handy when entering a whole "
                 "booster. Undo brings back what you had.")
                  .arg(lastAdded_.displayName)
            : (lastAdded_.has
                   ? tr("The last card you added (“%1”) had no comments to reuse.")
                         .arg(lastAdded_.displayName)
                   : tr("Available once you have added a card this session — it reuses "
                        "that card's comments.")));
    connect(reuseCommentsButton_, &QPushButton::clicked, this,
            &AddCardCopyPage::reuseLastComments);
    form_->addAction(reuseCommentsButton_);

    // Only in species mode: the name-search finder takes a card name, so pointing it at
    // a set would search nonsense — there the button simply doesn't exist.
    if (dexNumber_) {
        const QString setQuery = lastSetQuery(lastAdded_.setName, lastAdded_.expansionCode);
        const bool canSearchLastSet = lastAdded_.has && !setQuery.isEmpty();
        searchLastSetButton_ = new QPushButton(tr("Search last set"), this);
        searchLastSetButton_->setEnabled(canSearchLastSet);
        searchLastSetButton_->setToolTip(
            canSearchLastSet
                ? tr("List the printings of “%1” — the last card's set — in the search on "
                     "the right. Nothing is filled in until you pick a card from the "
                     "results.")
                      .arg(setQuery)
                : (lastAdded_.has
                       ? tr("The last card you added recorded no set to search.")
                       : tr("Available once you have added a card this session — it "
                            "searches that card's set.")));
        connect(searchLastSetButton_, &QPushButton::clicked, this,
                &AddCardCopyPage::searchLastSet);
        form_->addAction(searchLastSetButton_);
    }

    // --- Finder (right): the shared search + preview widget ----------------
    // Scoped: search the species' printings by set. Species-free: search by card name
    // (there is no dex number to scope by).
    finder_ = dexNumber_
                  ? new CardFinderPanel(search, *dexNumber_, speciesName, this)
                  : new CardFinderPanel(search, CardFinderPanel::NameSearchMode{}, QString(), this);
    // When a search has no printings, remind the user the form on the left still works.
    finder_->setNoResultsHint(tr("you can still fill the form by hand."));
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
    // The catalog has now said everything it can about this card, so the fields still empty
    // are exactly the ones it couldn't answer — raise the form's "⚠ not filled in for you"
    // markers on them (they clear themselves as the user fills each in).
    form_->setMissingFieldHints(true);
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
    // resolves from them). This is prefillFormFields plus a finder search.
    prefillFormFields(cardName, setName, setCode, collectorNumber);

    // Then drive the finder (name-search mode here) so the catalog lists this card's
    // printings and a pick autofills the full identity + image. The card name is the
    // name-mode query; fall back to the set when there's no name to search on.
    const QString query = !cardName.isEmpty() ? cardName : setName;
    if (!query.isEmpty()) {
        finder_->searchFor(query);
    }
}

void AddCardCopyPage::prefillFrom(const ScannedCard& scanned) {
    prefillFrom(QString::fromStdString(scanned.cardName),
                QString::fromStdString(scanned.setName),
                QString::fromStdString(scanned.setCode),
                QString::fromStdString(scanned.collectorNumber));
}

void AddCardCopyPage::prefillFormFields(const ScannedCard& scanned) {
    prefillFormFields(QString::fromStdString(scanned.cardName),
                      QString::fromStdString(scanned.setName),
                      QString::fromStdString(scanned.setCode),
                      QString::fromStdString(scanned.collectorNumber));
}

void AddCardCopyPage::prefillSetSearch(const QString& setQuery) {
    // Species-scoped add from a scan: drive the finder's set search only, leaving the form
    // blank so the user picks the printing (which autofills the identity + image). Nothing
    // is written to the form — the picked card is the deterministic source of truth.
    if (!setQuery.trimmed().isEmpty()) {
        finder_->searchFor(setQuery);
    }
}

void AddCardCopyPage::prefillFormFields(const QString& cardName, const QString& setName,
                                       const QString& setCode, const QString& collectorNumber) {
    // Paste the read fields onto the form and run NO search (the escape hatch for a flaky
    // catalog). setCardReference is silent (no referenceEdited) and leaves the form's
    // language/condition/ownership — so the default language pre-selection is kept.
    CardReference ref;
    ref.name = cardName.toStdString();
    ref.setName = setName.toStdString();
    ref.expansionCode = setCode.toStdString();
    ref.collectorNumber = collectorNumber.toStdString();
    form_->setCardReference(ref);
    // A reading has filled in what it could (here the scanner's, not the catalog's), so the
    // same "still empty, still optional" markers apply — see autofillFrom.
    form_->setMissingFieldHints(true);
    updateSubmitEnabled();  // the pasted collector number may already satisfy submit
}

void AddCardCopyPage::reuseLastComments() {
    if (!lastAdded_.has) {
        return;  // button is disabled in this case, but guard anyway
    }
    // The comment is the one thing a booster's cards share that neither the card search
    // nor Settings can supply, so it is all this button carries. It OVERWRITES the box:
    // the click is an explicit gesture, so it should always take effect (nothing here
    // touches the printed identity or gates submit). replaceComments, not setComments,
    // so a mis-click on a typed note is recoverable with Undo.
    form_->replaceComments(lastAdded_.comments);
}

void AddCardCopyPage::searchLastSet() {
    // Search only — this writes nothing to the form. The user still picks a printing
    // from the results to decide what autofills (a programmatic searchFor deliberately
    // doesn't emit setChosen, so even the set fields stay untouched until then).
    const QString setQuery = lastSetQuery(lastAdded_.setName, lastAdded_.expansionCode);
    if (!lastAdded_.has || setQuery.isEmpty()) {
        return;  // button is disabled in these cases, but guard anyway
    }
    finder_->searchFor(setQuery);
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
    // pristine (empty identity, unspecified condition, Owned, empty comments), so any
    // departure from that is worth confirming. A picked finder card is covered by the
    // identity check: it autofills those fields (and checkUnmatch drops the pick once
    // they're edited away), so it never needs a separate term here.
    // Language is the exception: the ctor pre-selects the user's Settings default, which
    // is OUR doing, not theirs — comparing against what we seeded (blank when there is no
    // default) is what stops Back on an untouched page from always asking to discard.
    const CardReference ref = form_->cardReference();
    if (!ref.name.empty() || !ref.expansionCode.empty() || !ref.setName.empty() ||
        !ref.collectorNumber.empty() || ref.language != seededLanguage_) {
        return true;
    }
    if (!form_->comments().empty() || form_->condition().has_value() ||
        form_->rarity().has_value() || form_->foil().has_value() ||
        form_->ownership() != CardOwnership::Owned) {
        return true;
    }
    // An uploaded-but-unsaved photo would be lost on Back — worth confirming.
    if (!uploadedImage_.isNull() || form_->noFixedPosition()) {
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
                                 form_->comments(), /*externalCardId=*/std::string(),
                                 form_->noFixedPosition());
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
    // Remember this copy's set, comments, and a display name so the next add page (a
    // fresh instance) can offer them in one click — the same-booster flow. The display
    // name prefers the printed card name, falling back to the species name.
    QString reuseName = QString::fromStdString(created.cardRef.name);
    if (reuseName.isEmpty()) {
        reuseName = speciesName_;
    }
    lastAdded_ = LastAdded{/*has=*/true, created.cardRef.expansionCode,
                           created.cardRef.setName, created.comments, reuseName};

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
