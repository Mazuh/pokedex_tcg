#pragma once

#include <QWidget>

class QStackedWidget;

namespace pokedex {

class BinderService;
class BinderGuideService;
class PokemonBrowseService;

// GUI — the application's top-level window: a macOS-style shell with a left
// sidebar (a source list, like Finder's or System Settings') selecting between
// sections shown on the right. Section 0 is the Binders page; section 1 is the
// unscoped Pokémon browser. It owns nothing but the layout — each section is a
// thin shell over its Qt-free service, all of which are owned by main() and
// outlive this window.
class MainWindow : public QWidget {
    Q_OBJECT

public:
    // `collectionPath` is the workspace folder, forwarded to the Binders page so
    // the user can always see where their collection lives.
    MainWindow(BinderService& binderService, BinderGuideService& guide,
               PokemonBrowseService& browse, const QString& collectionPath,
               QWidget* parent = nullptr);

private:
    QStackedWidget* sections_;
};

}  // namespace pokedex
