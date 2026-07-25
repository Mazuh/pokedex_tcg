#include "gui/views/edit_card_copy_page.h"

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

#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "gui/services/card_image_store.h"
#include "gui/views/back_button.h"
#include "gui/views/card_copy_form.h"
#include "gui/views/card_copy_splitter.h"
#include "gui/views/card_finder_panel.h"
#include "gui/views/card_prices_panel.h"
#include "gui/views/photo_upload.h"
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
      copy_(std::move(copy)),
      binders_(binders) {
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
    // Binder is editable — reassignment is supported here as it is elsewhere.
    form_->setupBinderPicker(binders, copy_.binderId, /*enabled=*/true);
    connect(form_, &CardCopyForm::binderChanged, this, &EditCardCopyPage::saveBinder);
    // Only the printed identity is locked; language/condition/ownership stay editable.
    form_->setReferenceEditable(false);

    saveButton_ = new QPushButton(tr("Save changes"), this);
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
    // finder searches by card name, seeded with the card's stored name.
    if (copy_.pokemonDexNum) {
        const QString species = title.section(QStringLiteral(" · "), 0, 0);
        finder_ = new CardFinderPanel(search, *copy_.pokemonDexNum, species, this);
    } else {
        finder_ = new CardFinderPanel(search, CardFinderPanel::NameSearchMode{},
                                      QString::fromStdString(copy_.cardRef.name), this);
    }
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

    // "Link prices to this card": how an existing copy (added before it was linked, or
    // by hand) gets an external_card_id so its prices can be fetched. Enabled as soon
    // as a card is picked (unlike the image button it needs no loaded preview), and
    // persists immediately via CardCopyService::linkCatalogCard.
    linkButton_ = new QPushButton(tr("Link prices to this card"), this);
    linkButton_->setEnabled(false);
    connect(linkButton_, &QPushButton::clicked, this, &EditCardCopyPage::linkFromFinder);

    connect(finder_, &CardFinderPanel::cardSelected, this, [this](const CardCandidate&) {
        useButton_->setEnabled(false);  // waits for the image to load (previewReady)
        linkButton_->setEnabled(true);  // a pick is all linking needs
    });
    connect(finder_, &CardFinderPanel::selectionCleared, this, [this]() {
        useButton_->setEnabled(false);
        linkButton_->setEnabled(false);
    });
    connect(finder_, &CardFinderPanel::previewReady, this,
            [this]() { useButton_->setEnabled(true); });

    // Both card-scoped actions sit under the preview, where the picture they apply to is.
    auto* footer = new QWidget(this);
    auto* footerLayout = new QVBoxLayout(footer);
    footerLayout->setContentsMargins(0, 0, 0, 0);
    footerLayout->addWidget(useButton_);
    footerLayout->addWidget(linkButton_);
    finder_->setPreviewFooter(footer);

    // --- Prices (below): the copy's market prices, on demand ---------------
    // Keyed by the copy's external catalog id; unlinked copies show a hint instead.
    // Strictly on-demand — the panel renders cached prices and only fetches when the
    // user clicks its button, so opening the edit page never hits the price API.
    prices_ = new CardPricesPanel(priceLookup, search, copies, this);
    prices_->showCopy(copy_);
    // If the Fetch button auto-resolves this copy's catalog link, keep our copy_ in sync
    // so a later save/refresh (and the manual-link button's enablement) sees it linked.
    connect(prices_, &CardPricesPanel::cardLinked, this,
            [this](const QString& copyId, const QString& externalCardId) {
                if (copyId.toStdString() == copy_.id) {
                    copy_.externalCardId = externalCardId.toStdString();
                }
            });

    // --- Assemble -----------------------------------------------------------
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(makeCardCopySplitter(form_, finder_), /*stretch=*/1);
    layout->addWidget(prices_);

    refreshCurrentImage();  // show whatever picture the copy currently has
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
    // editable fields against the stored copy.
    return form_->comments() != copy_.comments || form_->condition() != copy_.condition ||
           form_->rarity() != copy_.rarity || form_->foil() != copy_.foil ||
           form_->ownership() != copy_.ownership ||
           form_->cardReference().language != copy_.cardRef.language;
}

void EditCardCopyPage::updateSaveEnabled() { saveButton_->setEnabled(isDirty()); }

bool EditCardCopyPage::saveDetails() {
    // The identity fields are read-only, so form_->cardReference() carries the recorded
    // printing plus whatever language the user picked — editDetails persists that along
    // with ownership, condition, and comments in one write.
    try {
        copies_.editDetails(copy_.id, form_->cardReference(), form_->ownership(),
                            form_->condition(), form_->rarity(), form_->foil(),
                            form_->comments());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not save changes:\n%1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    // Mirror the write into the record so the button disables until re-edited.
    copy_.cardRef = form_->cardReference();
    copy_.ownership = form_->ownership();
    copy_.condition = form_->condition();
    copy_.rarity = form_->rarity();
    copy_.foil = form_->foil();
    copy_.comments = form_->comments();
    saveButton_->setEnabled(false);
    showToast(this, tr("Changes saved."));
    return true;
}

void EditCardCopyPage::saveBinder() {
    const std::optional<CardBinderId> target = form_->binderId();
    if (target == copy_.binderId) {
        return;  // no change (e.g. the user reselected the same entry)
    }
    try {
        copies_.assignToBinder(copy_.id, target);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not file the card:\n%1").arg(QString::fromUtf8(e.what())));
        // The write failed, so the combo now shows a binder the record doesn't have —
        // restore it to the stored value.
        form_->setupBinderPicker(binders_, copy_.binderId, /*enabled=*/true);
        return;
    }
    copy_.binderId = target;
    showToast(this, target ? tr("Card filed in its binder.")
                           : tr("Card removed from its binder."));
}

void EditCardCopyPage::handleBack() {
    // The editable details (language/condition/ownership/comments) live unsaved on this
    // page until "Save changes" (image and binder changes persist immediately). If any
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

void EditCardCopyPage::linkFromFinder() {
    if (!finder_->hasSelection()) {
        return;
    }
    const CardCandidate picked = finder_->selectedCandidate();
    try {
        copies_.linkCatalogCard(copy_.id, picked.id);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not link the card:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }
    copy_.externalCardId = picked.id;
    // Re-point the prices panel at the freshly-linked card; it shows the "Fetch prices"
    // button now that there is an id to look up (still on-demand — no auto-fetch).
    prices_->showCopy(copy_);
    showToast(this, tr("Linked — you can now fetch this card's prices below."));
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
