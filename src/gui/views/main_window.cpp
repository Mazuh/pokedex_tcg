#include "gui/views/main_window.h"

#include <QAction>
#include <QFrame>
#include <QHBoxLayout>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "gui/views/about_dialog.h"
#include "gui/views/binders_page.h"
#include "gui/views/owned_cards_view.h"
#include "gui/views/pokemon_list_view.h"
#include "gui/views/splitter_style.h"
#include "gui/views/wishlist_view.h"

namespace pokedex {

MainWindow::MainWindow(BinderService& binderService, BinderGuideService& guide,
                       PokemonBrowseService& browse, WishlistService& wishlist,
                       MediaService& media, CardSearchService& cardSearch,
                       CardPriceLookupService& priceLookup, CardCopyService& cardCopies,
                       CardImageStore& cardImages, const QString& collectionPath,
                       QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(tr("Pokedex TCG"));
    resize(900, 600);

    // Opens the modal About box, parented to the window. Shared by the menu-bar
    // item (macOS's application menu) and the in-window sidebar footer button, so
    // the About box is reachable without relying on the native menu bar.
    const auto showAbout = [this]() { AboutDialog(this).exec(); };

    // The left sidebar: a macOS-style source list whose rows select the section
    // shown on the right. No frame, so it reads as a pane rather than a boxed list.
    auto* sidebar = new QListWidget(this);
    sidebar->setFrameShape(QFrame::NoFrame);
    // Keep it a narrow, fixed-purpose pane: never a horizontal scrollbar (it
    // would clip labels like "All Pokémon"), and a bounded width so the split
    // divider can't be dragged to swallow the whole window or collapse the pane.
    sidebar->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
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

    // An "About" affordance pinned at the bottom of the sidebar, so the same dialog
    // is reachable inside the window (not only via the native macOS menu). Flat, so
    // it reads as a footer action rather than a primary button, but in the normal
    // text color (a divider above sets it apart) so it's plainly visible.
    auto* aboutButton = new QPushButton(tr("ⓘ  About"));
    aboutButton->setFlat(true);
    aboutButton->setCursor(Qt::PointingHandCursor);
    aboutButton->setStyleSheet(
        "QPushButton { border: none; text-align: left; padding: 14px 20px;"
        " color: palette(text); }"
        "QPushButton:hover { background: rgba(128, 128, 128, 0.20); }");

    // A hairline above the footer separates it from the source list.
    auto* footerDivider = new QFrame;
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setFrameShadow(QFrame::Plain);
    footerDivider->setStyleSheet("color: rgba(128, 128, 128, 0.35);");
    connect(aboutButton, &QPushButton::clicked, this, showAbout);

    // Wrap the list + footer into one pane; the pane (not the list) now carries the
    // sidebar's width bounds so the splitter treats the whole column as one band.
    auto* sidebarPanel = new QWidget(this);
    sidebarPanel->setMinimumWidth(160);
    sidebarPanel->setMaximumWidth(280);
    auto* sidebarLayout = new QVBoxLayout(sidebarPanel);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);
    sidebarLayout->addWidget(sidebar, 1);  // the list takes all the slack
    sidebarLayout->addWidget(footerDivider);
    sidebarLayout->addWidget(aboutButton);

    // Section order must match the sidebar row order above: a row selects the
    // stack page at the same index.
    sections_ = new QStackedWidget(this);
    sections_->addWidget(new BindersPage(binderService, guide, wishlist, media, cardSearch,
                                         priceLookup, cardCopies, cardImages, collectionPath));
    sections_->addWidget(new PokemonListView(browse, wishlist, media, cardSearch, priceLookup,
                                             cardCopies, cardImages, binderService));
    sections_->addWidget(
        new OwnedCardsView(cardCopies, binderService, cardImages, cardSearch, priceLookup));
    sections_->addWidget(new WishlistView(wishlist));

    connect(sidebar, &QListWidget::currentRowChanged, sections_,
            &QStackedWidget::setCurrentIndex);

    // A draggable split: a narrow sidebar that keeps its size while the section
    // area takes the slack when the window resizes.
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->addWidget(sidebarPanel);
    splitter->addWidget(sections_);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    // Neither pane may be dragged shut; the sidebar's min/max width keep it a
    // narrow band so a fast drag can't flip it to full-window width.
    splitter->setChildrenCollapsible(false);
    splitter->setSizes({180, 720});
    thinDivider(splitter);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    // A menu bar carrying a single About item. On macOS Qt promotes this to the
    // native global menu bar, and the AboutRole moves the item into the bold
    // "Pokédex TCG" application menu (the platform-standard spot for About);
    // elsewhere it renders in-window under Help. The dialog is parented to the
    // window so it centers over it and shares its lifetime.
    auto* menuBar = new QMenuBar(this);
    QMenu* helpMenu = menuBar->addMenu(tr("Help"));
    QAction* aboutAction = helpMenu->addAction(tr("About Pokédex TCG"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, showAbout);
    layout->setMenuBar(menuBar);

    sidebar->setCurrentRow(0);  // start on Binders
}

}  // namespace pokedex
