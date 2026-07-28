#include "gui/views/pokemon_detail_panel.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSizePolicy>
#include <QStyle>
#include <QStringList>
#include <QVBoxLayout>

#include <exception>
#include <optional>

#include "core/app/wishlist_service.h"
#include "gui/services/card_image_store.h"
#include "gui/services/card_price_lookup_service.h"
#include "gui/services/media_service.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/card_prices_summary.h"
#include "gui/views/condition_labels.h"
#include "gui/views/foil_labels.h"
#include "gui/views/rarity_labels.h"
#include "gui/views/scaled_pixmap.h"

namespace pokedex {

namespace {

// The card's display name: its printed card name (usually the species for a Pokémon
// card, the only label for a Trainer/Energy card), falling back to the species name
// when the card records none. Preferred over speciesOrCardName (which is species-first)
// because the inspector titles by the card, per the shared spec.
QString cardTitle(const CardCopy& copy) {
    const QString cardName = QString::fromStdString(copy.cardRef.name);
    if (!cardName.isEmpty()) {
        return cardName;
    }
    return copy.pokemonDexNum ? speciesName(*copy.pokemonDexNum) : QString();
}

// Join the non-empty parts of a detail line with " · " (a middle dot), so an unset
// field drops out rather than leaving a dangling separator.
QString joinParts(const QStringList& parts) {
    QStringList kept;
    for (const QString& part : parts) {
        if (!part.isEmpty()) {
            kept << part;
        }
    }
    return kept.join(QStringLiteral(" · "));
}

}  // namespace

PokemonDetailPanel::PokemonDetailPanel(MediaService& media, WishlistService& wishlist,
                                       CardImageStore* images, CardPriceLookupService* prices,
                                       CardCopyService* copies, QWidget* parent)
    : QWidget(parent), media_(media), wishlist_(wishlist), images_(images) {
    name_ = new QLabel(this);
    name_->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    name_->setWordWrap(true);
    QFont nameFont = name_->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 3);
    name_->setFont(nameFont);

    // The printed collector identity ("BS 44/102"), a muted line under the name; shown
    // only in copy mode (a plain species has no printed card).
    collector_ = new QLabel(this);
    collector_->setAlignment(Qt::AlignHCenter);
    collector_->setEnabled(false);  // muted, secondary info

    image_ = new QLabel(this);
    image_->setAlignment(Qt::AlignCenter);
    image_->setMinimumSize(160, 160);
    image_->setEnabled(false);  // muted placeholder text until an image arrives
    // Ignore the pixmap's own size hint so the label takes exactly the space the
    // layout gives it (down to the minimum) and never grows to fit a large scan.
    image_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    // Rescale whenever the label itself resizes — not only on a panel resize. Showing
    // the copy-detail block / edit button shrinks this label without resizing the
    // panel, so a panel-only resize hook would leave the pixmap scaled to the old,
    // taller height and the QLabel would clip it top and bottom (centered overflow).
    image_->installEventFilter(this);

    // Copy-detail block (copy mode only): one condition/foil line, one rarity/count line,
    // and the copy's comments. Kept deliberately spare — set/number sits above the art
    // and ownership is omitted so the card image dominates. Hidden outside copy mode.
    condFoilLine_ = new QLabel(this);
    condFoilLine_->setAlignment(Qt::AlignHCenter);
    rarityCountLine_ = new QLabel(this);
    rarityCountLine_->setAlignment(Qt::AlignHCenter);
    rarityCountLine_->setEnabled(false);  // muted, secondary info
    copyComments_ = new QLabel(this);
    copyComments_->setWordWrap(true);
    copyComments_->setAlignment(Qt::AlignHCenter);
    copyComments_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    copyDetail_ = new QWidget(this);
    auto* copyLayout = new QVBoxLayout(copyDetail_);
    copyLayout->setContentsMargins(0, 0, 0, 0);
    copyLayout->addWidget(condFoilLine_);
    copyLayout->addWidget(rarityCountLine_);
    copyLayout->addWidget(copyComments_);

    // "Edit card" opens the copy's edit page. Relayed up (the panel is embedded and
    // can't host the page itself), like the add-copy action.
    editButton_ = new QPushButton(tr("Edit"), this);
    editButton_->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
    connect(editButton_, &QPushButton::clicked, this, [this]() {
        if (!shownCopyId_.isEmpty()) {
            Q_EMIT editCopyRequested(shownCopyId_);
        }
    });

    // Add action. Relays the request upward (the panel is embedded, so it can't host
    // the page itself). Its flow — a species copy vs. a species-free card — is chosen by
    // setAddMode; both signals are wired here and the mode picks which fires.
    addButton_ = new QPushButton(this);
    addButton_->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
    connect(addButton_, &QPushButton::clicked, this, [this]() {
        if (addMode_ == AddMode::FreeCard) {
            Q_EMIT addCardRequested();
        } else if (currentDex_ >= 0) {
            Q_EMIT addCopyRequested(currentDex_, speciesName_);
        }
    });

    // Add + Edit side by side.
    auto* buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 0, 0, 0);
    buttonRow->addWidget(addButton_);
    buttonRow->addWidget(editButton_);

    // Market-prices block (copy mode only): the per-vendor headline + marketplace links + ⓘ, an
    // inline Fetch/Refresh, and a "Manage prices" button that opens the dedicated page. Built only
    // when both price services were supplied (they come with the image store from the hosts);
    // hidden outside copy mode. Manage relays up so the host pushes the PricesEditPage; an inline
    // fetch that links a copy relays up as copyLinked so the host updates its cached copy.
    if (prices && copies) {
        priceSummary_ = new CardPricesSummary(*prices, *copies, this);
        connect(priceSummary_, &CardPricesSummary::managePricesRequested, this,
                &PokemonDetailPanel::managePricesRequested);
        connect(priceSummary_, &CardPricesSummary::copyLinked, this,
                &PokemonDetailPanel::copyLinked);
    }

    // The wishlist button sits below the art. It carries the sources count in its label
    // ("Wishlist (2)" / "Wishlist (none)") and opens the species' wishlist on its own
    // screen. The request is relayed up (the panel is embedded, so it can't host the
    // page itself). Hidden on surfaces that hold species-free cards (My Cards).
    wishlistButton_ = new QPushButton(this);
    connect(wishlistButton_, &QPushButton::clicked, this, [this]() {
        if (currentDex_ >= 0) {
            Q_EMIT editWishlistRequested(currentDex_, speciesName_);
        }
    });

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addWidget(name_);
    layout->addWidget(collector_);
    layout->addWidget(image_, /*stretch=*/1);
    layout->addWidget(copyDetail_);
    if (priceSummary_) {
        layout->addWidget(priceSummary_);
    }
    layout->addLayout(buttonRow);
    layout->addWidget(wishlistButton_);

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

void PokemonDetailPanel::setAddMode(AddMode mode) {
    addMode_ = mode;
    updateButtons();
}

void PokemonDetailPanel::setWishlistVisible(bool visible) {
    wishlistVisible_ = visible;
    updateButtons();
}

void PokemonDetailPanel::showPokemon(int dexNumber, const QString& name) {
    showPokemon(dexNumber, name, {});
}

void PokemonDetailPanel::showPokemon(int dexNumber, const QString& name,
                                     const std::vector<CardCopy>& ownedCopiesHere,
                                     const QString& preferCopyId) {
    currentDex_ = dexNumber;
    speciesName_ = name;
    name_->setText(name);
    updateWishlistButton(dexNumber);

    // Copy mode: a species owned here (and a store to read the image from). Show
    // `preferCopyId` if it names one of the copies (so a just-edited copy stays on
    // screen across the edit round-trip); otherwise pick one at random, as requested
    // for a fresh selection. renderCopy() drives the image (the copy's scan, or the
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
        renderCopy(ownedCopiesHere[index], static_cast<int>(ownedCopiesHere.size()));
        return;
    }

    // Plain mode: no owned copy → generic artwork, no copy widgets.
    hideCopy();
    requestArtworkFallback();
}

void PokemonDetailPanel::showSingleCopy(const CardCopy& copy, int sameSpeciesTotal) {
    // My Cards' exact-copy entry: the species (if any) drives only the artwork fallback
    // and the copy count; the wishlist button is hidden on this surface.
    currentDex_ = copy.pokemonDexNum ? *copy.pokemonDexNum : -1;
    speciesName_ = copy.pokemonDexNum ? speciesName(*copy.pokemonDexNum) : QString();
    renderCopy(copy, sameSpeciesTotal);
}

void PokemonDetailPanel::renderCopy(const CardCopy& copy, int copyTotal) {
    shownCopyId_ = QString::fromStdString(copy.id);
    shownCopyRemoved_ = copy.ownership == CardOwnership::Removed;

    // Title by the card (falling back to the species), with the printed identity on its
    // own muted line beneath — the set (abbreviation, or the full set name when there's
    // no abbreviation) plus the collector number.
    name_->setText(cardTitle(copy));
    const QString collector = collectorLine(copy.cardRef);
    collector_->setText(collector);
    collector_->setVisible(!collector.isEmpty());

    // One line of condition + foil; one line of rarity + copy count. Each shows only the
    // parts that are set, and hides entirely when it would be blank — an unset value adds
    // no "Unspecified" clutter.
    const QString condition = copy.condition ? conditionAbbrev(*copy.condition) : QString();
    const QString foil = copy.foil ? foilLabel(*copy.foil) : QString();
    const QString condFoil = joinParts({condition, foil});
    condFoilLine_->setText(condFoil);
    condFoilLine_->setVisible(!condFoil.isEmpty());

    const QString rarity = copy.rarity ? rarityLabel(*copy.rarity) : QString();
    // "N copies" is how many copies of this species exist on the surface — a total (the
    // callers exclude soft-Removed copies, which are frozen history); 0 hides it (a
    // species-free card, which has no species to count).
    QString count;
    if (copyTotal > 0) {
        count = copyTotal == 1 ? tr("1 copy") : tr("%1 copies").arg(copyTotal);
    }
    const QString rarityCount = joinParts({rarity, count});
    rarityCountLine_->setText(rarityCount);
    rarityCountLine_->setVisible(!rarityCount.isEmpty());

    // A long comment must not push the image, prices, and buttons off-panel: cap the
    // shown text at a few lines' worth of characters and append an ellipsis, keeping the
    // full comment on a tooltip (and selectable up to the cap). This bounds the label's
    // height so the rest of the detail panel keeps its space. The Edit page shows the
    // whole comment for reading/editing.
    const QString comments = QString::fromStdString(copy.comments).trimmed();
    constexpr int kMaxCommentChars = 180;
    const bool truncated = comments.size() > kMaxCommentChars;
    copyComments_->setText(truncated ? comments.left(kMaxCommentChars).trimmed() +
                                           QStringLiteral("…")
                                     : comments);
    copyComments_->setToolTip(truncated ? comments : QString());
    copyComments_->setVisible(!comments.isEmpty());

    copyDetail_->show();
    // Prices for this copy (cached-only on show; the Fetch button spends the network and
    // invisibly resolves the catalog link when the copy isn't linked yet).
    if (priceSummary_) {
        priceSummary_->showCopy(copy);
        priceSummary_->show();
    }
    updateButtons();

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
    // A species-free card (no dex) has no Pokémon artwork to fall back to — show the
    // "no image" placeholder rather than firing a bogus artwork request for dex < 1.
    if (currentDex_ < 1) {
        placeholder_ = tr("No image saved for this card.");
        renderImage();
        return;
    }
    placeholder_ = tr("Loading…");
    renderImage();
    media_.request({currentDex_, speciesName_.toStdString()}, MediaKind::OfficialArtwork);
}

void PokemonDetailPanel::hideCopy() {
    shownCopyId_.clear();
    shownCopyRemoved_ = false;
    collector_->hide();
    copyDetail_->hide();
    if (priceSummary_) {
        priceSummary_->clear();
        priceSummary_->hide();
    }
    updateButtons();
}

void PokemonDetailPanel::clear() {
    currentDex_ = -1;
    speciesName_.clear();
    name_->clear();
    showingCopyImage_ = false;
    originalPixmap_ = QPixmap();
    image_->setEnabled(false);  // muted placeholder, even after a prior load enabled it
    placeholder_ = tr("Select a card to see its details.");
    renderImage();
    wishlistButton_->setText(tr("Wishlist"));
    wishlistButton_->setEnabled(false);
    hideCopy();
}

void PokemonDetailPanel::updateButtons() {
    // Add is always shown; its enablement and flow depend on the mode. FreeCard (My
    // Cards) records a species-free card and needs no selection, so it stays enabled
    // even in the empty state; SpeciesCopy needs a shown species.
    addButton_->setText(tr("Add"));
    addButton_->setEnabled(addMode_ == AddMode::FreeCard || currentDex_ >= 0);
    // Edit acts on the shown copy — only meaningful in copy mode, and never on a
    // soft-Removed copy (frozen history the service refuses to edit).
    editButton_->setVisible(!shownCopyId_.isEmpty() && !shownCopyRemoved_);
    // Wishlist is a species affordance; hidden entirely on species-free surfaces.
    wishlistButton_->setVisible(wishlistVisible_);
}

void PokemonDetailPanel::updateWishlistButton(int dexNumber) {
    // Read the current source count so the button advertises it ("Wishlist (2)" vs.
    // "Wishlist (none)"). A best-effort read: a storage hiccup shows the plain label
    // rather than crashing the panel — the wishlist page itself is the source of truth.
    int count = 0;
    try {
        if (const std::optional<Wishlist> wl = wishlist_.forPokemon(dexNumber)) {
            count = static_cast<int>(wl->sources.size());
        }
    } catch (const std::exception&) {
        count = 0;
    }
    wishlistButton_->setText(count == 0 ? tr("Wishlist (none)")
                                        : tr("Wishlist (%1)").arg(count));
    wishlistButton_->setEnabled(true);
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

bool PokemonDetailPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == image_ && event->type() == QEvent::Resize) {
        renderImage();  // rescale the image to the label's new size
    }
    return QWidget::eventFilter(watched, event);
}

}  // namespace pokedex
