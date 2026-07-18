#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

class QLabel;

namespace pokedex {

// GUI — the right-hand detail panel on "My Cards": shows the selected copy's title
// (species + printed identity) and its stored card image. Mirrors
// PokemonDetailPanel's image render/scale idiom, but is a pure display widget — no
// MediaService, no wishlist, no add-copy button. The owning OwnedCardsView loads
// the image (a synchronous local disk read via CardImageStore) and hands it in, so
// this panel neither fetches nor knows where images live.
//
// showImage() displays the title, the scaled image (or a muted "no image saved"
// placeholder when the pixmap is null — an older copy, or one added without a preview
// selected), and the copy's comments beneath it (hidden when blank). clear() is the
// no-selection empty state. The image rescales to the pane on resize.
class CardImagePanel : public QWidget {
    Q_OBJECT

public:
    explicit CardImagePanel(QWidget* parent = nullptr);

    // Show `title`, `image`, and `comments`. A null `image` renders the "no image
    // saved" placeholder in its place; blank `comments` hides the comments area. Not
    // named show() so it doesn't hide QWidget::show().
    void showImage(const QString& title, const QPixmap& image, const QString& comments);
    // Empty state: no card selected.
    void clear();

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    // Scale originalPixmap_ to fit the image label, or show placeholder_ text when
    // there is no pixmap (empty / no-image states).
    void renderImage();

    QLabel* title_;
    QLabel* image_;
    QLabel* comments_;        // the copy's comments, beneath the image (hidden when blank)
    QPixmap originalPixmap_;  // full-resolution card image; rescaled on resize
    QString placeholder_;     // text shown when there is no pixmap
};

}  // namespace pokedex
