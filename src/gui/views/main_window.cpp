#include "gui/views/main_window.h"

#include <QAction>
#include <QCloseEvent>
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
#include "gui/views/assistant_prompt_dialog.h"
#include "gui/views/binders_page.h"
#include "gui/views/owned_cards_view.h"
#include "gui/views/pokemon_list_view.h"
#include "gui/views/scan_card_dialog.h"
#include "gui/views/settings_view.h"
#include "gui/views/splitter_style.h"
#include "gui/views/wishlist_view.h"

namespace pokedex {

MainWindow::MainWindow(BinderService& binderService, BinderGuideService& guide,
                       PokemonBrowseService& browse, WishlistService& wishlist,
                       MediaService& media, CardSearchService& cardSearch,
                       CardPriceLookupService& priceLookup, CardCopyService& cardCopies,
                       CardImageStore& cardImages, AssistantService& assistant,
                       const QString& collectionPath, QWidget* parent)
    : QWidget(parent) {
    setWindowTitle(tr("Pokedex TCG"));
    resize(900, 600);

    // Opens the modal About box, parented to the window. Shared by the menu-bar
    // item (macOS's application menu) and the in-window sidebar footer button, so
    // the About box is reachable without relying on the native menu bar.
    const auto showAbout = [this]() { AboutDialog(this).exec(); };

    // Opens the AI-assistant demo, parented to the window. Shared by the sidebar
    // footer button and the Tools menu action.
    const auto showAssistant = [this, &assistant]() {
        AssistantPromptDialog(assistant, this).exec();
    };

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
    new QListWidgetItem(tr("Settings"), sidebar);

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

    // A sibling footer action opening the card scanner (webcam → assistant reads the
    // card → search), styled to match About. This is the primary entry to the scan flow;
    // the AI-assistant demo stays reachable from the Tools menu.
    auto* scanButton = new QPushButton(tr("✦  Scan card"));
    scanButton->setFlat(true);
    scanButton->setCursor(Qt::PointingHandCursor);
    scanButton->setStyleSheet(
        "QPushButton { border: none; text-align: left; padding: 14px 20px;"
        " color: palette(text); }"
        "QPushButton:hover { background: rgba(128, 128, 128, 0.20); }");

    // A hairline above the footer separates it from the source list.
    auto* footerDivider = new QFrame;
    footerDivider->setFrameShape(QFrame::HLine);
    footerDivider->setFrameShadow(QFrame::Plain);
    footerDivider->setStyleSheet("color: rgba(128, 128, 128, 0.35);");
    connect(aboutButton, &QPushButton::clicked, this, showAbout);
    // scanButton is connected below, once the "My Cards" section it routes into exists.

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
    sidebarLayout->addWidget(scanButton);
    sidebarLayout->addWidget(aboutButton);

    // Section order must match the sidebar row order above: a row selects the
    // stack page at the same index.
    sections_ = new QStackedWidget(this);
    sections_->addWidget(new BindersPage(binderService, guide, wishlist, media, cardSearch,
                                         priceLookup, cardCopies, cardImages, collectionPath));
    auto* pokemonView = new PokemonListView(browse, wishlist, media, cardSearch, priceLookup,
                                            cardCopies, cardImages, binderService);
    sections_->addWidget(pokemonView);
    auto* ownedView = new OwnedCardsView(cardCopies, binderService, cardImages, cardSearch,
                                         priceLookup, media, wishlist);
    sections_->addWidget(ownedView);
    sections_->addWidget(new WishlistView(wishlist));
    settings_ = new SettingsView;
    const int settingsRow = sections_->addWidget(settings_);

    // The Settings section is the one page with a manually-applied form, so leaving
    // it must not silently drop unsaved edits. Route section switches through a guard
    // that, when leaving Settings with a dirty form, prompts Save/Discard/Cancel — and
    // on Cancel snaps the sidebar selection back without re-prompting (guarding_).
    connect(sidebar, &QListWidget::currentRowChanged, this,
            [this, sidebar, settingsRow](int row) {
                if (row < 0 || guarding_) {
                    return;
                }
                if (row == currentRow_) {
                    sections_->setCurrentIndex(row);
                    return;
                }
                if (currentRow_ == settingsRow && !settings_->confirmLeave(this)) {
                    // Snap the selection back to Settings. Deferred (queued) because the
                    // list is still mid-keypress/mid-click here — reverting inline gets
                    // overwritten when that handler finishes moving the selection, leaving
                    // the highlight on the new row while the page stays on Settings.
                    QMetaObject::invokeMethod(
                        this,
                        [this, sidebar]() {
                            guarding_ = true;
                            sidebar->setCurrentRow(currentRow_);  // no re-prompt
                            guarding_ = false;
                        },
                        Qt::QueuedConnection);
                    return;
                }
                currentRow_ = row;
                sections_->setCurrentIndex(row);
            });

    // Double-clicking an owned species in the Pokémon browser jumps to "My Cards"
    // pre-filtered to that species: set the filter first (it persists through the
    // reload the section's showEvent triggers), then select the sidebar row — which
    // switches sections and highlights it, keeping the sidebar in sync. Row 2 = My
    // Cards, matching the sidebar/section order built above.
    connect(pokemonView, &PokemonListView::searchInMyCardsRequested, this,
            [sidebar, ownedView](const QString& species) {
                ownedView->searchFor(species);
                sidebar->setCurrentRow(2);
            });

    // Opens the webcam Scan-a-card dialog. When it resolves a reading, route the query
    // into "My Cards" (fills its search so the user sees whether they already own it, and
    // stashes the scan so the next "Add a card" pre-fills from it) and switch there.
    const auto showScan = [this, &assistant, sidebar, ownedView]() {
        ScanCardDialog dialog(assistant, this);
        connect(&dialog, &ScanCardDialog::cardResolved, this,
                [sidebar, ownedView](const ScannedCard& scanned) {
                    ownedView->applyScannedCard(scanned);
                    sidebar->setCurrentRow(2);  // My Cards
                });
        dialog.exec();
    };
    connect(scanButton, &QPushButton::clicked, this, showScan);

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
    QMenu* toolsMenu = menuBar->addMenu(tr("Tools"));
    QAction* scanAction = toolsMenu->addAction(tr("Scan a card…"));
    connect(scanAction, &QAction::triggered, this, showScan);
    QAction* assistantAction = toolsMenu->addAction(tr("AI Assistant…"));
    connect(assistantAction, &QAction::triggered, this, showAssistant);
    QMenu* helpMenu = menuBar->addMenu(tr("Help"));
    QAction* aboutAction = helpMenu->addAction(tr("About Pokédex TCG"));
    aboutAction->setMenuRole(QAction::AboutRole);
    connect(aboutAction, &QAction::triggered, this, showAbout);
    layout->setMenuBar(menuBar);

    sidebar->setCurrentRow(0);  // start on Binders
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // Only relevant while Settings is the visible section; when it's not, its form is
    // clean (leaving it required saving/discarding), so confirmLeave() is a no-op.
    if (!settings_->confirmLeave(this)) {
        event->ignore();
        return;
    }
    QWidget::closeEvent(event);
}

}  // namespace pokedex
