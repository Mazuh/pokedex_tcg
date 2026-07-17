#include "gui/views/main_window.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>

#include "gui/views/binders_page.h"
#include "gui/views/owned_cards_view.h"
#include "gui/views/pokemon_list_view.h"
#include "gui/views/splitter_style.h"
#include "gui/views/wishlist_view.h"

namespace pokedex {

MainWindow::MainWindow(BinderService& binderService, BinderGuideService& guide,
                       PokemonBrowseService& browse, WishlistService& wishlist,
                       MediaService& media, CardSearchService& cardSearch,
                       CardCopyService& cardCopies, const QString& collectionPath,
                       QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(tr("Pokedex TCG"));
    resize(900, 600);

    // The left sidebar: a macOS-style source list whose rows select the section
    // shown on the right. No frame, so it reads as a pane rather than a boxed list.
    auto* sidebar = new QListWidget(this);
    sidebar->setFrameShape(QFrame::NoFrame);
    // Breathing room so rows don't hug the window edge, and rounded selection
    // pills — the macOS source-list look.
    sidebar->setStyleSheet(
        "QListWidget { padding: 8px 6px; }"
        "QListWidget::item { padding: 6px 8px; border-radius: 6px; }"
        "QListWidget::item:selected { background: palette(highlight);"
        " color: palette(highlighted-text); }"
        // When the window isn't focused, macOS greys the selection — but keep it
        // clearly legible (a plain palette(midlight) is nearly invisible in dark
        // mode), so the current section always reads.
        "QListWidget::item:selected:!active { background: rgba(128, 128, 128, 0.32);"
        " color: palette(text); }");
    new QListWidgetItem(tr("Binders"), sidebar);
    new QListWidgetItem(tr("All Pokémon"), sidebar);
    new QListWidgetItem(tr("My Cards"), sidebar);
    new QListWidgetItem(tr("Wishlist"), sidebar);

    // Section order must match the sidebar row order above: a row selects the
    // stack page at the same index.
    sections_ = new QStackedWidget(this);
    sections_->addWidget(new BindersPage(binderService, guide, wishlist, media, cardSearch,
                                         cardCopies, collectionPath));
    sections_->addWidget(
        new PokemonListView(browse, wishlist, media, cardSearch, cardCopies, binderService));
    sections_->addWidget(new OwnedCardsView(cardCopies, binderService));
    sections_->addWidget(new WishlistView(wishlist));

    connect(sidebar, &QListWidget::currentRowChanged, sections_,
            &QStackedWidget::setCurrentIndex);

    // A draggable split: a narrow sidebar that keeps its size while the section
    // area takes the slack when the window resizes.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(sidebar);
    splitter->addWidget(sections_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({180, 720});
    thinDivider(splitter);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    sidebar->setCurrentRow(0);  // start on Binders
}

}  // namespace pokedex
