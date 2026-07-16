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

// GUI — the application's top-level window: a macOS-style shell with a left
// sidebar (a source list, like Finder's or System Settings') selecting between
// sections shown on the right. Section 0 is the Binders page; section 1 is the
// unscoped Pokémon browser; section 2 is the unscoped Wishlist. It owns nothing
// but the layout — each section is a thin shell over its Qt-free service, all of
// which are owned by main() and outlive this window.
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
               const QString& collectionPath, QWidget* parent = nullptr);

private:
    QStackedWidget* sections_;
};

}  // namespace pokedex
