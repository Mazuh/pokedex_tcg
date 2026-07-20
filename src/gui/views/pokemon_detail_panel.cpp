#include "gui/views/pokemon_detail_panel.h"

#include <QFont>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QStyle>
#include <QVBoxLayout>

#include "gui/services/card_image_store.h"
#include "gui/services/media_service.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/condition_labels.h"
#include "gui/views/ownership_labels.h"
#include "gui/views/scaled_pixmap.h"
#include "gui/views/wishlist_sources_editor.h"

namespace pokedex {

PokemonDetailPanel::PokemonDetailPanel(MediaService& media, WishlistService& wishlist,
                                       CardImageStore* images, QWidget* parent)
    : QWidget(parent), media_(media), wishlist_(wishlist), images_(images) {
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

    // Copy-detail block (copy mode only): the owned copy's printed identity,
    // condition, ownership, a count of copies filed here, and its comments. Hidden
    // outside copy mode. Sits between the art and the add-copy action.
    copyIdentity_ = new QLabel(this);
    copyIdentity_->setAlignment(Qt::AlignHCenter);
    copyIdentity_->setWordWrap(true);
    QFont identityFont = copyIdentity_->font();
    identityFont.setBold(true);
    copyIdentity_->setFont(identityFont);
    copyCondition_ = new QLabel(this);
    copyCondition_->setAlignment(Qt::AlignHCenter);
    copyOwnership_ = new QLabel(this);
    copyOwnership_->setAlignment(Qt::AlignHCenter);
    copyCounter_ = new QLabel(this);
    copyCounter_->setAlignment(Qt::AlignHCenter);
    copyCounter_->setEnabled(false);  // muted, secondary info
    copyComments_ = new QLabel(this);
    copyComments_->setWordWrap(true);
    copyComments_->setAlignment(Qt::AlignHCenter);
    copyComments_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    copyDetail_ = new QWidget(this);
    auto* copyLayout = new QVBoxLayout(copyDetail_);
    copyLayout->setContentsMargins(0, 0, 0, 0);
    copyLayout->addWidget(copyIdentity_);
    copyLayout->addWidget(copyCondition_);
    copyLayout->addWidget(copyOwnership_);
    copyLayout->addWidget(copyCounter_);
    copyLayout->addWidget(copyComments_);

    // "Edit card" opens the copy's edit page. Relayed up (the panel is embedded and
    // can't host the page itself), like the add-copy action.
    editButton_ = new QPushButton(tr("Edit card…"), this);
    connect(editButton_, &QPushButton::clicked, this, [this]() {
        if (!shownCopyId_.isEmpty()) {
            Q_EMIT editCopyRequested(shownCopyId_);
        }
    });

    // Add-copy action sits between the art and the wishlist editor. It relays the
    // request upward (the panel is embedded, so it can't host the page itself).
    addCopyButton_ = new QPushButton(tr("Add copy…"), this);
    addCopyButton_->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    connect(addCopyButton_, &QPushButton::clicked, this, [this]() {
        if (currentDex_ >= 0) {
            Q_EMIT addCopyRequested(currentDex_, name_->text());
        }
    });

    // The wishlist sources editor sits below the art. The artwork keeps the
    // stretch so it takes the slack; the editor stays at its natural height.
    wishlistEditor_ = new WishlistSourcesEditor(wishlist_, this);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addWidget(name_);
    layout->addWidget(image_, /*stretch=*/1);
    layout->addWidget(copyDetail_);
    layout->addWidget(editButton_);
    layout->addWidget(addCopyButton_);
    layout->addWidget(wishlistEditor_);

    // `this` is the receiver, so Qt auto-disconnects when the panel is destroyed
    // — an in-flight fetch that completes after a binder view closes is harmless.
    connect(&media_, &MediaService::ready, this, &PokemonDetailPanel::onReady);
    connect(&media_, &MediaService::failed, this, &PokemonDetailPanel::onFailed);
    // A deferred download / hand-placed override for the shown copy re-reads the file.
    if (images_) {
        connect(images_, &CardImageStore::imageChanged, this,
                &PokemonDetailPanel::onCardImageChanged);
    }

    clear();
}

void PokemonDetailPanel::showPokemon(int dexNumber, const QString& name) {
    showPokemon(dexNumber, name, {});
}

void PokemonDetailPanel::showPokemon(int dexNumber, const QString& name,
                                     const std::vector<CardCopy>& ownedCopiesHere,
                                     const QString& preferCopyId) {
    currentDex_ = dexNumber;
    name_->setText(name);
    addCopyButton_->setEnabled(true);
    wishlistEditor_->setPokemon(dexNumber);

    // Copy mode: a species owned here (and a store to read the image from). Show
    // `preferCopyId` if it names one of the copies (so a just-edited copy stays on
    // screen across the edit round-trip); otherwise pick one at random, as requested
    // for a fresh selection. showCopy() drives the image (the copy's scan, or the
    // artwork as fallback).
    if (images_ && !ownedCopiesHere.empty()) {
        int index = -1;
        if (!preferCopyId.isEmpty()) {
            for (int i = 0; i < static_cast<int>(ownedCopiesHere.size()); ++i) {
                if (QString::fromStdString(ownedCopiesHere[i].id) == preferCopyId) {
                    index = i;
                    break;
                }
            }
        }
        if (index < 0) {
            index = static_cast<int>(QRandomGenerator::global()->bounded(
                static_cast<quint32>(ownedCopiesHere.size())));
        }
        showCopy(ownedCopiesHere[index], static_cast<int>(ownedCopiesHere.size()));
        return;
    }

    // Plain mode: no owned copy → generic artwork, no copy widgets.
    hideCopy();
    requestArtworkFallback();
}

void PokemonDetailPanel::showCopy(const CardCopy& copy, int total) {
    shownCopyId_ = QString::fromStdString(copy.id);

    const QString set = QString::fromStdString(copy.cardRef.setName);
    const QString code = cardText(copy.cardRef);
    QString identity = set;
    if (!code.isEmpty()) {
        identity = set.isEmpty() ? code : set + QStringLiteral(" · ") + code;
    }
    copyIdentity_->setText(identity);
    copyIdentity_->setVisible(!identity.isEmpty());

    copyCondition_->setText(
        tr("Condition: %1")
            .arg(copy.condition ? conditionLabel(*copy.condition) : tr("Ungraded")));
    copyOwnership_->setText(tr("Ownership: %1").arg(ownershipLabel(copy.ownership)));
    // The browser aggregates a species' copies across all binders, so "filed here"
    // names no location there (copiesAcrossBinders_); the binder guide keeps it.
    if (copiesAcrossBinders_) {
        copyCounter_->setText(total == 1 ? tr("1 owned copy")
                                         : tr("%1 owned copies").arg(total));
    } else {
        copyCounter_->setText(total == 1 ? tr("1 owned copy filed here")
                                         : tr("%1 owned copies filed here").arg(total));
    }

    const QString comments = QString::fromStdString(copy.comments);
    copyComments_->setText(comments);
    copyComments_->setVisible(!comments.isEmpty());

    copyDetail_->show();
    editButton_->show();

    // Image: the copy's card scan when it has one, else fall back to the Pokémon
    // artwork (request it and let onReady fill in; showingCopyImage_ stays false).
    showCopyImage(copy.id);
}

void PokemonDetailPanel::showCopyImage(const std::string& copyId) {
    const QPixmap scan = images_->load(copyId);
    if (!scan.isNull()) {
        showingCopyImage_ = true;
        originalPixmap_ = scan;
        image_->setEnabled(true);
        renderImage();
        return;
    }
    requestArtworkFallback();
}

void PokemonDetailPanel::requestArtworkFallback() {
    // Loading state: drop any previous image and show the placeholder while the fetch
    // is in flight. Re-mute the label (a prior load enabled it) so the placeholder
    // reads as a muted hint, not full-strength content.
    showingCopyImage_ = false;
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);
    placeholder_ = tr("Loading…");
    renderImage();
    media_.request({currentDex_, name_->text().toStdString()}, MediaKind::OfficialArtwork);
}

void PokemonDetailPanel::hideCopy() {
    shownCopyId_.clear();
    copyDetail_->hide();
    editButton_->hide();
}

void PokemonDetailPanel::clear() {
    currentDex_ = -1;
    name_->clear();
    showingCopyImage_ = false;
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);  // muted placeholder, even after a prior load enabled it
    placeholder_ = tr("Select a Pokémon to see its artwork.");
    renderImage();
    addCopyButton_->setEnabled(false);
    wishlistEditor_->clear();
    hideCopy();
}

void PokemonDetailPanel::onReady(int dexNumber, MediaKind kind, const QPixmap& pixmap) {
    Q_UNUSED(kind);
    if (dexNumber != currentDex_) {
        return;  // stale: the user moved on to another Pokémon
    }
    if (showingCopyImage_) {
        return;  // a copy scan is on screen; the artwork was only a fallback we don't need
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
    if (showingCopyImage_) {
        return;  // showing the copy scan; the failed artwork fallback is irrelevant
    }
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);
    placeholder_ = tr("No image available.");
    renderImage();
}

void PokemonDetailPanel::onCardImageChanged(const QString& copyId) {
    if (shownCopyId_.isEmpty() || copyId != shownCopyId_) {
        return;  // not the copy currently on screen
    }
    // Re-read the shown copy's scan; if it was removed, showCopyImage falls back
    // to the artwork.
    showCopyImage(shownCopyId_.toStdString());
}

void PokemonDetailPanel::renderImage() {
    if (originalPixmap_.isNull()) {
        image_->setText(placeholder_);
        return;
    }
    setScaledPixmap(image_, originalPixmap_);
}

void PokemonDetailPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    renderImage();  // rescale the artwork to the new panel size
}

}  // namespace pokedex
