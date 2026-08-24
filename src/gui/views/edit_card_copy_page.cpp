#include "gui/views/edit_card_copy_page.h"

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

#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "gui/services/card_image_store.h"
#include "gui/views/back_button.h"
#include "gui/views/card_copy_form.h"
#include "gui/views/card_copy_splitter.h"
#include "gui/views/card_finder_panel.h"
#include "gui/views/card_prices_summary.h"
#include "gui/views/photo_upload.h"
#include "gui/views/prices_edit_page.h"
#include "gui/views/primary_button.h"
#include "gui/views/scaled_pixmap.h"
#include "gui/views/toast.h"

namespace pokedex {

EditCardCopyPage::EditCardCopyPage(CardSearchService& search, CardPriceLookupService& priceLookup,
                                   CardImageStore& images, CardCopyService& copies, CardCopy copy,
                                   const std::vector<CardBinder>& binders,
                                   const QString& title, QWidget* parent)
    : QWidget(parent),
      images_(images),
      copies_(copies),
      priceLookup_(priceLookup),
      copy_(std::move(copy)) {
    // --- Top bar: Back + heading + current-image thumbnail -----------------
    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &EditCardCopyPage::handleBack);

    auto* heading = new QLabel(tr("Edit card — %1").arg(title), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    // The copy's current stored image, small, on the right of the top bar so it stays
    // visible while editing (the finder preview to the right shows a *candidate*, not
    // the card's actual picture). refreshCurrentImage() fills it now and again whenever
    // an image save lands (Use this card's image / Upload a photo).
    currentImage_ = new QLabel(this);
    currentImage_->setAlignment(Qt::AlignCenter);
    // A fixed box so the thumbnail can't widen the top bar — a portrait card fills
    // it; a landscape upload fit-scales down and centers inside it.
    currentImage_->setFixedSize(54, 72);
    currentImage_->setEnabled(false);  // muted: it's context, not an action
    connect(&images_, &CardImageStore::imageChanged, this, [this](const QString& copyId) {
        if (copyId.toStdString() == copy_.id) {
            refreshCurrentImage();
            // Keep the finder's preview placeholder in step with the stored image, so a
            // fresh upload/pick shows there too once nothing is selected.
            if (finder_ != nullptr) {
                finder_->setPreviewPlaceholder(images_.load(copy_.id));
            }
        }
    });

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();
    topBar->addWidget(new QLabel(tr("Current image:"), this));
    topBar->addWidget(currentImage_);

    // --- Form (left): the shared details pane, read-only but for comments --
    form_ = new CardCopyForm(this);
    form_->setMaximumWidth(560);
    form_->loadCopy(copy_);
    // Binder is editable — reassignment is supported here as it is elsewhere. An explicit
    // "Remove from binder" button beside the combo makes unassigning discoverable rather
    // than hiding it behind the "— None —" entry; it reports through the same
    // binderChanged() signal a manual pick does, so both stage the same way.
    form_->setupBinderPicker(binders, copy_.binderId, /*enabled=*/true);
    form_->setBinderRemovable(true);
    // Take the baseline from what the picker actually SHOWS, not from the record. A host
    // can hand us a copy whose binder is no longer in the list — PokemonListView caches its
    // copies behind CardCopyService::revision(), which BinderService::remove doesn't bump,
    // so a deleted binder lingers in that cache (the DB already unassigned the copy via ON
    // DELETE SET NULL). fillBinderCombo quietly falls back to "— None —" there, and with the
    // record's dead id as the baseline the page would open permanently dirty — Save lit and
    // Back prompting over a card nobody touched. Re-reading it costs nothing when they agree
    // (an unchanged field is never written) and is exactly right when they don't: the page
    // cannot represent a binder it wasn't given.
    copy_.binderId = form_->binderId();
    // Same reasoning, same fix, for the two picker fields: a copy can carry a rarity or
    // foil the picker no longer OFFERS (see CardRarityGroup::Retired / foilIsRetired), and
    // loadCopy shows those as "— None —" because the combo has no item for them. With the
    // record's value as the baseline the page would open permanently dirty over a card
    // nobody touched — and since Back's prompt defaults to Save, merely opening Edit to
    // READ such a copy and pressing Back+Enter would silently clear the value. Taking the
    // baseline from the form confines the loss to a deliberate Save, which is the whole
    // deal a withdrawn option is offered on.
    copy_.rarity = form_->rarity();
    copy_.foil = form_->foil();
    // The filing fields stage like every other field on the form: they only mark the page
    // dirty here, and "Save changes" commits them with the rest. They used to write the
    // instant they were touched, which made two of a dozen fields behave unlike the other
    // ten — the checkbox especially, since a tickbox reads as form state you are expected
    // to save. One form, one commit.
    connect(form_, &CardCopyForm::binderChanged, this, &EditCardCopyPage::updateSaveEnabled);
    connect(form_, &CardCopyForm::noFixedPositionChanged, this,
            &EditCardCopyPage::updateSaveEnabled);
    // Only the printed identity is locked; language/condition/ownership stay editable.
    form_->setReferenceEditable(false);

    saveButton_ = new QPushButton(tr("Save changes"), this);
    applyPrimaryButtonStyle(saveButton_);  // the primary/commit action — accent + ✓
    saveButton_->setEnabled(false);  // enabled only once an editable field diverges
    connect(saveButton_, &QPushButton::clicked, this, &EditCardCopyPage::saveDetails);
    form_->addAction(saveButton_);

    auto* uploadButton = new QPushButton(tr("Upload a photo…"), this);
    connect(uploadButton, &QPushButton::clicked, this, &EditCardCopyPage::uploadPhoto);
    form_->addAction(uploadButton);

    // "Save changes" is live only while an editable field (comments, or a
    // language/condition/ownership pick) differs from the stored record.
    connect(form_, &CardCopyForm::commentsChanged, this, &EditCardCopyPage::updateSaveEnabled);
    connect(form_, &CardCopyForm::detailsChanged, this, &EditCardCopyPage::updateSaveEnabled);

    // --- Finder (right): the shared search + preview widget ----------------
    // A species copy scopes the finder to that species (its name is the title's lead
    // segment). A species-free card (Trainer/Energy) has no dex to scope by, so the
    // finder searches by card name.
    //
    // Editing is usually a comment/rarity tweak, not an image swap — so when the copy
    // already has a picture, don't auto-run a search on open (which would fetch a page
    // of results and a batch of thumbnails the user rarely wants). The species finder
    // never auto-searches; the name finder only seeds — and thus searches — when there
    // is a reason to (no current picture). The current picture is shown in the finder
    // preview instead, so the user still sees what they're editing without a network hit.
    // (Pricing no longer needs this finder: the prices panel resolves a copy's tcgdex card
    // directly from its set+number, so the finder here is purely for the image.)
    const QPixmap currentPixmap = images_.load(copy_.id);
    if (copy_.pokemonDexNum) {
        const QString species = title.section(QStringLiteral(" · "), 0, 0);
        finder_ = new CardFinderPanel(search, *copy_.pokemonDexNum, species, this);
    } else {
        const QString seed =
            currentPixmap.isNull() ? QString::fromStdString(copy_.cardRef.name) : QString();
        finder_ = new CardFinderPanel(search, CardFinderPanel::NameSearchMode{}, seed, this);
    }
    finder_->setPreviewPlaceholder(currentPixmap);
    // When a set has no printings (e.g. a card too new for the catalog), point the
    // user at the upload path instead.
    finder_->setNoResultsHint(
        tr("the catalog may not list it yet — you can upload a photo instead."));

    // "Use this card's image" sits centered under the preview (where the picture it
    // applies to is), enabled only once that card's large image has actually loaded —
    // so the save is synchronous and the host's My Cards refresh shows it immediately.
    useButton_ = new QPushButton(tr("Use this card's image"), this);
    useButton_->setEnabled(false);
    QFont useFont = useButton_->font();
    useFont.setBold(true);
    useButton_->setFont(useFont);
    useButton_->setMinimumHeight(38);
    connect(useButton_, &QPushButton::clicked, this, &EditCardCopyPage::saveFromFinder);

    connect(finder_, &CardFinderPanel::cardSelected, this, [this](const CardCandidate&) {
        useButton_->setEnabled(false);  // waits for the image to load (previewReady)
    });
    connect(finder_, &CardFinderPanel::selectionCleared, this,
            [this]() { useButton_->setEnabled(false); });
    connect(finder_, &CardFinderPanel::previewReady, this,
            [this]() { useButton_->setEnabled(true); });

    // The image action sits under the preview, where the picture it applies to is.
    finder_->setPreviewFooter(useButton_);

    // --- Prices (below): the copy's market prices, read-only -----------------
    // The compact figures + marketplace links, keyed by the copy's external catalog id.
    // Every action (Fetch/Refresh, Clear, hide/restore) lives behind its "Manage prices"
    // button, which pushes the dedicated PricesEditPage onto this page's own inner stack —
    // so opening the edit page never hits the price API and its cramped layout stays clean.
    priceSummary_ = new CardPricesSummary(priceLookup, copies, this);
    priceSummary_->showCopy(copy_);
    connect(priceSummary_, &CardPricesSummary::managePricesRequested, this,
            [this](const QString&) { openPrices(); });
    // An inline fetch on the summary can resolve + link this copy; keep copy_ in sync so a later
    // save/refresh (and the prices page opened here) sees it linked.
    connect(priceSummary_, &CardPricesSummary::copyLinked, this,
            [this](const QString& copyId, const QString& externalCardId) {
                if (copyId.toStdString() == copy_.id) {
                    copy_.externalCardId = externalCardId.toStdString();
                }
            });

    // --- Assemble -----------------------------------------------------------
    // The edit content is page 0 of an inner stack; the prices page pushes over it (like the
    // sections' list ⇄ page idiom) so managing prices stays in-window without a second Back
    // fighting this page's own Back.
    auto* contentPage = new QWidget(this);
    auto* layout = new QVBoxLayout(contentPage);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(makeCardCopySplitter(form_, finder_), /*stretch=*/1);
    layout->addWidget(priceSummary_);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(contentPage);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(stack_);

    refreshCurrentImage();  // show whatever picture the copy currently has
}

void EditCardCopyPage::openPrices() {
    // Push the dedicated prices page over the edit content. A Fetch there can invisibly resolve
    // this copy's tcgdex link; keep copy_ in sync so a later save/refresh sees it linked. On
    // Back, re-point the summary at the (possibly newly linked / re-priced) copy — its
    // externalCardId may have changed, which the pricesReady signal alone wouldn't surface.
    auto* page = new PricesEditPage(priceLookup_, copies_, copy_);
    connect(page, &PricesEditPage::cardLinked, this,
            [this](const QString& copyId, const QString& externalCardId) {
                if (copyId.toStdString() == copy_.id) {
                    copy_.externalCardId = externalCardId.toStdString();
                }
            });
    connect(page, &PricesEditPage::backRequested, this, [this, page]() {
        stack_->setCurrentIndex(0);
        stack_->removeWidget(page);
        page->deleteLater();
        priceSummary_->showCopy(copy_);
    });
    stack_->addWidget(page);
    stack_->setCurrentWidget(page);
}

void EditCardCopyPage::refreshCurrentImage() {
    const QPixmap current = images_.load(copy_.id);
    if (current.isNull()) {
        currentImage_->setPixmap(QPixmap());
        currentImage_->setText(tr("(none)"));
        return;
    }
    // Fit-scale within the fixed box (DPR-aware) via the shared helper, so the
    // thumbnail never exceeds its bounds regardless of the image's aspect ratio.
    setScaledPixmap(currentImage_, current);
}

bool EditCardCopyPage::isDirty() const {
    // The printed identity is read-only, so form_->cardReference() equals the record
    // except for language (the one reference field that stays editable). Compare the
    // editable fields against the stored copy — ALL of them, including the two filing
    // fields, which is what lets Back guard them too.
    return form_->comments() != copy_.comments || form_->condition() != copy_.condition ||
           form_->rarity() != copy_.rarity || form_->foil() != copy_.foil ||
           form_->ownership() != copy_.ownership ||
           form_->cardReference().language != copy_.cardRef.language ||
           form_->binderId() != copy_.binderId ||
           form_->noFixedPosition() != copy_.noFixedPosition;
}

void EditCardCopyPage::updateSaveEnabled() { saveButton_->setEnabled(isDirty()); }

bool EditCardCopyPage::saveDetails() {
    // One gesture, but three service verbs — the details, the binder, and the loose flag
    // are separate writes (each re-reads the copy, so the order between them is free).
    // They run in sequence and each is mirrored into copy_ as it lands, so a failure
    // part-way through reports itself and leaves the page holding exactly what is still
    // unsaved; nothing on the form is silently reverted.
    const auto persist = [this](const QString& failure, auto&& write) {
        try {
            write();
            return true;
        } catch (const std::exception& e) {
            QMessageBox::warning(this, tr("Pokedex TCG"),
                                 failure.arg(QString::fromUtf8(e.what())));
            return false;
        }
    };

    // The identity fields are read-only, so form_->cardReference() carries the recorded
    // printing plus whatever language the user picked — editDetails persists that along
    // with ownership, condition, and comments in one write.
    if (!persist(tr("Could not save changes:\n%1"), [this] {
            copies_.editDetails(copy_.id, form_->cardReference(), form_->ownership(),
                                form_->condition(), form_->rarity(), form_->foil(),
                                form_->comments());
        })) {
        return false;
    }
    copy_.cardRef = form_->cardReference();
    copy_.ownership = form_->ownership();
    copy_.condition = form_->condition();
    copy_.rarity = form_->rarity();
    copy_.foil = form_->foil();
    copy_.comments = form_->comments();

    if (const std::optional<CardBinderId> target = form_->binderId(); target != copy_.binderId) {
        if (!persist(tr("Could not file the card:\n%1"),
                     [this, &target] { copies_.assignToBinder(copy_.id, target); })) {
            return false;
        }
        copy_.binderId = target;
    }

    // Note this reads the box even while it is disabled (no binder picked), which is
    // deliberate in BOTH directions: unfiling a loose card leaves the flag as it was, so
    // refiling it restores the setting rather than silently dropping it — and ticking the
    // box and then unfiling in the same visit likewise saves the tick, for a card that is
    // now in no binder. The flag only means anything to a binder guide, so a stored one on
    // an unfiled card is inert, and clearing it here would break the restore property the
    // first half of this rule exists for.
    if (const bool loose = form_->noFixedPosition(); loose != copy_.noFixedPosition) {
        if (!persist(tr("Could not refile the card:\n%1"),
                     [this, loose] { copies_.setNoFixedPosition(copy_.id, loose); })) {
            return false;
        }
        copy_.noFixedPosition = loose;
    }

    updateSaveEnabled();  // every field now matches the record, so this disables the button
    showToast(this, tr("Changes saved."));
    return true;
}

void EditCardCopyPage::handleBack() {
    // Every editable field on the form — details, comments, binder, "no fixed position" —
    // lives unsaved on this page until "Save changes". (The image is the one exception:
    // its two buttons ARE the commit, so pressing one is never ambiguous.) If any field
    // diverges from the record, don't drop the edit silently on Back — offer to save it,
    // discard it, or stay. (Save failing keeps the user here so nothing is lost.)
    if (isDirty()) {
        const auto choice = QMessageBox::question(
            this, tr("Unsaved changes"),
            tr("You have unsaved changes to this card."),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel) {
            return;  // stay on the page
        }
        if (choice == QMessageBox::Save && !saveDetails()) {
            return;  // the save failed (already reported) — don't leave and lose it
        }
    }
    Q_EMIT backRequested();
}

void EditCardCopyPage::saveFromFinder() {
    if (!finder_->hasSelection()) {
        return;
    }
    // The button is only enabled once the preview loaded, so this pixmap is non-null
    // — a synchronous save (no download to race the host's refresh).
    const QPixmap preview = finder_->selectedPreview();
    if (preview.isNull()) {
        return;  // defensive: nothing loaded to save
    }
    images_.save(copy_.id, preview);  // emits CardImageStore::imageChanged → host refresh
    showToast(this, tr("Image updated from the selected card."));
}

void EditCardCopyPage::uploadPhoto() {
    const std::optional<QPixmap> pixmap = pickCardPhoto(this);
    if (!pixmap) {
        return;  // cancelled, or unreadable (pickCardPhoto already warned)
    }
    if (!images_.save(copy_.id, *pixmap)) {  // emits imageChanged on success → host refresh
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("The image could not be saved to your workspace."));
        return;
    }
    showToast(this, tr("Image updated from the uploaded photo."));
}

}  // namespace pokedex
