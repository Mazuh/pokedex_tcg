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

namespace pokedex {

EditCardCopyPage::EditCardCopyPage(CardSearchService& search, CardImageStore& images,
                                   CardCopyService& copies, CardCopy copy,
                                   const std::vector<CardBinder>& binders,
                                   const QString& title, QWidget* parent)
    : QWidget(parent), images_(images), copies_(copies), copy_(std::move(copy)) {
    // --- Top bar: Back + heading -------------------------------------------
    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &EditCardCopyPage::backRequested);

    auto* heading = new QLabel(tr("Edit card — %1").arg(title), this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    heading->setFont(headingFont);

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    // --- Form (left): the shared details pane, read-only but for comments --
    form_ = new CardCopyForm(this);
    form_->setMaximumWidth(560);
    form_->loadCopy(copy_);
    form_->setupBinderPicker(binders, copy_.binderId, /*enabled=*/false);
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
    status_ = new QLabel(this);
    status_->setEnabled(false);  // muted: a transient confirmation, not content

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(makeCardCopySplitter(form_, finder_), /*stretch=*/1);
    layout->addWidget(status_);
}

void EditCardCopyPage::saveComments() {
    try {
        // Condition is read-only here, so form_->condition() is the recorded value —
        // editDetails only really changes the comments.
        copies_.editDetails(copy_.id, form_->condition(), form_->comments());
    } catch (const std::exception& e) {
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("Could not save comments:\n%1").arg(QString::fromUtf8(e.what())));
        return;
    }
    copy_.comments = form_->comments();  // record it so the button disables until re-edited
    saveComments_->setEnabled(false);
    status_->setText(tr("Comments saved."));
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
    status_->setText(tr("Image updated from the selected card."));
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
    status_->setText(tr("Image updated from the uploaded photo."));
}

}  // namespace pokedex
