#include "gui/views/card_image_panel.h"

#include <QFont>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "gui/views/scaled_pixmap.h"

namespace pokedex {

CardImagePanel::CardImagePanel(QWidget* parent) : QWidget(parent) {
    title_ = new QLabel(this);
    title_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    title_->setWordWrap(true);
    QFont titleFont = title_->font();
    titleFont.setBold(true);
    title_->setFont(titleFont);

    image_ = new QLabel(this);
    image_->setAlignment(Qt::AlignCenter);
    image_->setMinimumSize(160, 160);
    image_->setEnabled(false);  // muted placeholder text until an image is shown

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addWidget(title_);
    layout->addWidget(image_, /*stretch=*/1);

    clear();
}

void CardImagePanel::showImage(const QString& title, const QPixmap& image) {
    title_->setText(title);
    originalPixmap_ = image;
    if (image.isNull()) {
        image_->setEnabled(false);  // muted: a hint, not content
        placeholder_ = tr("No image saved for this card.");
    } else {
        image_->setEnabled(true);
    }
    renderImage();
}

void CardImagePanel::clear() {
    title_->clear();
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);  // muted placeholder, even after a prior image
    placeholder_ = tr("Select a card to see its image.");
    renderImage();
}

void CardImagePanel::renderImage() {
    if (originalPixmap_.isNull()) {
        image_->setText(placeholder_);
        return;
    }
    setScaledPixmap(image_, originalPixmap_);
}

void CardImagePanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    renderImage();  // rescale the image to the new panel size
}

}  // namespace pokedex
