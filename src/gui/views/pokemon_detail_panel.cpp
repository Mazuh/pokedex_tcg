#include "gui/views/pokemon_detail_panel.h"

#include <QFont>
#include <QLabel>
#include <QResizeEvent>
#include <QVBoxLayout>

#include "gui/services/media_service.h"
#include "gui/views/wishlist_sources_editor.h"

namespace pokedex {

PokemonDetailPanel::PokemonDetailPanel(MediaService& media, WishlistService& wishlist,
                                       QWidget* parent)
    : QWidget(parent), media_(media), wishlist_(wishlist) {
    name_ = new QLabel(this);
    name_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    name_->setWordWrap(true);
    QFont nameFont = name_->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 3);
    name_->setFont(nameFont);

    image_ = new QLabel(this);
    image_->setAlignment(Qt::AlignCenter);
    image_->setMinimumSize(160, 160);
    image_->setEnabled(false);  // muted placeholder text until an image arrives

    // The wishlist sources editor sits below the art. The artwork keeps the
    // stretch so it takes the slack; the editor stays at its natural height.
    wishlistEditor_ = new WishlistSourcesEditor(wishlist_, this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addWidget(name_);
    layout->addWidget(image_, /*stretch=*/1);
    layout->addWidget(wishlistEditor_);

    // `this` is the receiver, so Qt auto-disconnects when the panel is destroyed
    // — an in-flight fetch that completes after a binder view closes is harmless.
    connect(&media_, &MediaService::ready, this, &PokemonDetailPanel::onReady);
    connect(&media_, &MediaService::failed, this, &PokemonDetailPanel::onFailed);

    clear();
}

void PokemonDetailPanel::showPokemon(int dexNumber, const QString& name) {
    currentDex_ = dexNumber;
    name_->setText(name);
    // Loading state: drop any previous image and show the placeholder while the
    // fetch is in flight. Re-mute the label (a prior load enabled it) so the
    // placeholder reads as a muted hint, not full-strength content.
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);
    placeholder_ = tr("Loading…");
    renderImage();
    media_.request({dexNumber, name.toStdString()}, MediaKind::OfficialArtwork);
    wishlistEditor_->setPokemon(dexNumber);
}

void PokemonDetailPanel::clear() {
    currentDex_ = -1;
    name_->clear();
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);  // muted placeholder, even after a prior load enabled it
    placeholder_ = tr("Select a Pokémon to see its artwork.");
    renderImage();
    wishlistEditor_->clear();
}

void PokemonDetailPanel::onReady(int dexNumber, MediaKind kind, const QPixmap& pixmap) {
    Q_UNUSED(kind);
    if (dexNumber != currentDex_) {
        return;  // stale: the user moved on to another Pokémon
    }
    originalPixmap_ = pixmap;
    image_->setEnabled(true);
    renderImage();
}

void PokemonDetailPanel::onFailed(int dexNumber, MediaKind kind) {
    Q_UNUSED(kind);
    if (dexNumber != currentDex_) {
        return;  // stale
    }
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);
    placeholder_ = tr("No image available.");
    renderImage();
}

void PokemonDetailPanel::renderImage() {
    if (originalPixmap_.isNull()) {
        image_->setText(placeholder_);
        return;
    }
    // Scale to fit the label while keeping aspect ratio; account for the device
    // pixel ratio so artwork stays crisp on Retina displays.
    const qreal dpr = devicePixelRatioF();
    const QSize target = image_->size() * dpr;
    QPixmap scaled =
        originalPixmap_.scaled(target, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    image_->setPixmap(scaled);
}

void PokemonDetailPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    renderImage();  // rescale the artwork to the new panel size
}

}  // namespace pokedex
