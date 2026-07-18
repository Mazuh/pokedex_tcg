#include "gui/views/edit_card_copy_page.h"

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <exception>
#include <utility>

#include "core/app/card_catalog_dto.h"
#include "core/app/card_copy_service.h"
#include "gui/services/card_image_store.h"
#include "gui/views/back_button.h"
#include "gui/views/card_copy_form.h"
#include "gui/views/card_copy_splitter.h"
#include "gui/views/card_finder_panel.h"
#include "gui/views/scaled_pixmap.h"
#include "gui/views/toast.h"

namespace pokedex {

EditCardCopyPage::EditCardCopyPage(CardSearchService& search, CardImageStore& images,
                                   CardCopyService& copies, CardCopy copy,
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
    form_->setReferenceEditable(false);  // identity/condition/ownership are the record

    saveComments_ = new QPushButton(tr("Save comments"), this);
    saveComments_->setEnabled(false);  // enabled only once the comments diverge
    connect(saveComments_, &QPushButton::clicked, this, &EditCardCopyPage::saveComments);
    form_->addAction(saveComments_);

    auto* uploadButton = new QPushButton(tr("Upload a photo…"), this);
    connect(uploadButton, &QPushButton::clicked, this, &EditCardCopyPage::uploadPhoto);
    form_->addAction(uploadButton);

    // "Save comments" is live only while the text differs from the stored record.
    connect(form_, &CardCopyForm::commentsChanged, this, [this]() {
        saveComments_->setEnabled(form_->comments() != copy_.comments);
    });

    // --- Finder (right): the shared search + preview widget ----------------
    const QString species = title.section(QStringLiteral(" · "), 0, 0);
    finder_ = new CardFinderPanel(search, copy_.pokemonDexNum, species, this);
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
    connect(finder_, &CardFinderPanel::cardSelected, this,
            [this](const CardCandidate&) { useButton_->setEnabled(false); });
    connect(finder_, &CardFinderPanel::selectionCleared, this,
            [this]() { useButton_->setEnabled(false); });
    connect(finder_, &CardFinderPanel::previewReady, this,
            [this]() { useButton_->setEnabled(true); });
    finder_->setPreviewFooter(useButton_);

    // --- Assemble -----------------------------------------------------------
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(makeCardCopySplitter(form_, finder_), /*stretch=*/1);

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

bool EditCardCopyPage::saveComments() {
    try {
        // Condition is read-only here, so form_->condition() is the recorded value —
        // editDetails only really changes the comments.
        copies_.editDetails(copy_.id, form_->condition(), form_->comments());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not save comments:\n%1").arg(QString::fromUtf8(e.what())));
        return false;
    }
    copy_.comments = form_->comments();  // record it so the button disables until re-edited
    saveComments_->setEnabled(false);
    showToast(this, tr("Comments saved."));
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
    // Comments are the only editable field that lives unsaved on this page (image
    // changes save immediately). If the box diverges from the record, don't drop the
    // typing silently on Back — offer to save it, discard it, or stay. (Save failing
    // keeps the user here so nothing is lost.)
    if (form_->comments() != copy_.comments) {
        const auto choice = QMessageBox::question(
            this, tr("Unsaved comments"),
            tr("You have unsaved changes to this card's comments."),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (choice == QMessageBox::Cancel) {
            return;  // stay on the page
        }
        if (choice == QMessageBox::Save && !saveComments()) {
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
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Choose a card photo"), QString(),
        tr("Images (*.png *.jpg *.jpeg *.webp *.bmp)"));
    if (path.isEmpty()) {
        return;  // cancelled
    }
    const QPixmap pixmap(path);
    if (pixmap.isNull()) {
        QMessageBox::warning(
            this, tr("Pokedex TCG"),
            tr("That file could not be read as an image. Try a PNG or JPEG."));
        return;
    }
    if (!images_.save(copy_.id, pixmap)) {  // emits imageChanged on success → host refresh
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("The image could not be saved to your workspace."));
        return;
    }
    showToast(this, tr("Image updated from the uploaded photo."));
}

}  // namespace pokedex
