#pragma once

#include <QWidget>

class QStackedWidget;

namespace pokedex {

class BinderService;
class BinderGuideService;
class PokemonBrowseService;
class WishlistService;
class MediaService;
class CardSearchService;
class CardPriceLookupService;
class CardCopyService;
class CardImageStore;

// GUI — the application's top-level window: a macOS-style shell with a left
// sidebar (a source list, like Finder's or System Settings') selecting between
// sections shown on the right. Section 0 is the Binders page; section 1 is the
// unscoped Pokémon browser; section 2 is the unscoped Wishlist. It owns nothing
// but the layout — each section is a thin shell over its Qt-free service, all of
// which are owned by main() and outlive this window.
class SettingsView;

class MainWindow : public QWidget {
    Q_OBJECT

public:
    // `collectionPath` is the workspace folder, forwarded to the Binders page so
    // the user can always see where their collection lives. `media` is the shared
    // artwork fetch/cache service, threaded to the sections that show a Pokémon
    // detail panel.
    MainWindow(BinderService& binderService, BinderGuideService& guide,
               PokemonBrowseService& browse, WishlistService& wishlist,
               MediaService& media, CardSearchService& cardSearch,
               CardPriceLookupService& priceLookup, CardCopyService& cardCopies,
               CardImageStore& cardImages, const QString& collectionPath,
               QWidget* parent = nullptr);

protected:
    // Guard the window close the same way a section switch is guarded: if the
    // Settings form holds unsaved edits, prompt Save/Discard/Cancel and ignore the
    // close on Cancel.
    void closeEvent(QCloseEvent* event) override;

private:
    QStackedWidget* sections_;
    SettingsView* settings_;
    // The section currently shown; the leave-guard needs the row we're leaving, which
    // QListWidget::currentRowChanged doesn't report on its own.
    int currentRow_ = -1;
    // True while the guard is programmatically reverting the sidebar selection, so its
    // own currentRowChanged handler ignores that synthetic change (no re-prompt).
    bool guarding_ = false;
};

}  // namespace pokedex
