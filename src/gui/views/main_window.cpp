#include "gui/views/main_window.h"

#include <QHBoxLayout>
#include <QListWidget>
#include <QSplitter>
#include <QStackedWidget>

#include "gui/views/binders_page.h"
#include "gui/views/pokemon_list_view.h"

namespace pokedex {

MainWindow::MainWindow(BinderService& binderService, BinderGuideService& guide,
                       PokemonBrowseService& browse, MediaService& media,
                       const QString& collectionPath, QWidget* parent)
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
        "QListWidget::item { padding: 6px 8px; border-radius: 6px; }");
    new QListWidgetItem(tr("Binders"), sidebar);
    new QListWidgetItem(tr("Pokémon"), sidebar);

    sections_ = new QStackedWidget(this);
    sections_->addWidget(new BindersPage(binderService, guide, media, collectionPath));
    sections_->addWidget(new PokemonListView(browse, media));

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

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    sidebar->setCurrentRow(0);  // start on Binders
}

}  // namespace pokedex
