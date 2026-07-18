#include "gui/views/edit_card_copy_page.h"

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <utility>

#include "core/app/card_catalog_dto.h"
#include "gui/services/card_image_store.h"
#include "gui/views/back_button.h"
#include "gui/views/card_finder_panel.h"

namespace pokedex {

EditCardCopyPage::EditCardCopyPage(CardSearchService& search, CardImageStore& images,
                                   CardCopyId copyId, int dexNumber, const QString& title,
                                   QWidget* parent)
    : QWidget(parent), images_(images), copyId_(std::move(copyId)) {
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

    // --- The shared card finder (search + preview) -------------------------
    // Use the species name from the title's leading segment for the finder hint;
    // the title is "Species · Card", so anything before " · " is the species.
    const QString species = title.section(QStringLiteral(" · "), 0, 0);
    finder_ = new CardFinderPanel(search, dexNumber, species, this);
    // When a set has no printings (e.g. a card too new for the catalog), point the
    // user at the upload path instead.
    finder_->setNoResultsHint(
        tr("the catalog may not list it yet — you can upload a photo instead."));

    // --- Action bar: upload (left) + use-picked-card (right) ---------------
    auto* uploadButton = new QPushButton(tr("Upload a photo…"), this);
    connect(uploadButton, &QPushButton::clicked, this, &EditCardCopyPage::uploadPhoto);

    useButton_ = new QPushButton(tr("Use this card's image"), this);
    useButton_->setEnabled(false);  // until a picked card's image has loaded
    connect(useButton_, &QPushButton::clicked, this, &EditCardCopyPage::saveFromFinder);

    // Enable only once the picked card's large image is in hand, so saveFromFinder()
    // always has a real pixmap (a synchronous save, so the host's refresh is instant
    // and never races an async download). A new pick (or a cleared one) disables it
    // again until its image loads.
    connect(finder_, &CardFinderPanel::cardSelected, this,
            [this](const CardCandidate&) { useButton_->setEnabled(false); });
    connect(finder_, &CardFinderPanel::selectionCleared, this,
            [this]() { useButton_->setEnabled(false); });
    connect(finder_, &CardFinderPanel::previewReady, this,
            [this]() { useButton_->setEnabled(true); });

    auto* actions = new QHBoxLayout;
    actions->addWidget(uploadButton);
    actions->addStretch();
    actions->addWidget(useButton_);

    // --- Assemble -----------------------------------------------------------
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->addLayout(topBar);
    layout->addWidget(finder_, /*stretch=*/1);
    layout->addLayout(actions);
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
    images_.save(copyId_, preview);  // emits CardImageStore::imageChanged → host refresh
    Q_EMIT backRequested();
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
    if (!images_.save(copyId_, pixmap)) {  // emits imageChanged on success → host refresh
        QMessageBox::warning(this, tr("Pokedex TCG"),
                             tr("The image could not be saved to your workspace."));
        return;
    }
    Q_EMIT backRequested();
}

}  // namespace pokedex
