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

    comments_ = new QLabel(this);
    comments_->setWordWrap(true);
    comments_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    comments_->setTextInteractionFlags(Qt::TextSelectableByMouse);  // content: selectable
    comments_->hide();

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addWidget(title_);
    layout->addWidget(image_, /*stretch=*/1);
    layout->addWidget(comments_);

    clear();
}

void CardImagePanel::showImage(const QString& title, const QPixmap& image,
                               const QString& comments) {
    title_->setText(title);
    originalPixmap_ = image;
    if (image.isNull()) {
        image_->setEnabled(false);  // muted: a hint, not content
        placeholder_ = tr("No image saved for this card.");
    } else {
        image_->setEnabled(true);
    }
    comments_->setText(comments);
    comments_->setVisible(!comments.isEmpty());
    renderImage();
}

void CardImagePanel::clear() {
    title_->clear();
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);  // muted placeholder, even after a prior image
    placeholder_ = tr("Select a card to see its image.");
    comments_->clear();
    comments_->hide();
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
