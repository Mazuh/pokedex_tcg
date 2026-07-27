#include "gui/views/wishlist_edit_page.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "gui/views/back_button.h"
#include "gui/views/wishlist_sources_editor.h"

namespace pokedex {

WishlistEditPage::WishlistEditPage(WishlistService& wishlist, int dexNumber,
                                   const QString& speciesName, QWidget* parent)
    : QWidget(parent) {
    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &WishlistEditPage::backRequested);

    // The species name headlines the screen (the editor below carries the "Wishlist"
    // section heading, so the two together read "<species> · Wishlist" without repeating
    // the word). Bold and a touch larger, matching the detail panel's name.
    auto* heading = new QLabel(speciesName, this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    auto* subtitle = new QLabel(tr("Sellers and links to track for buying this card."), this);
    subtitle->setEnabled(false);  // muted: a hint, not content

    // The reusable sources CRUD, pointed at this species. It writes straight through
    // WishlistService, so leaving via Back needs no save step.
    auto* editor = new WishlistSourcesEditor(wishlist, this);
    editor->setPokemon(dexNumber);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addLayout(topBar);
    layout->addWidget(subtitle);
    layout->addWidget(editor);
    layout->addStretch();  // keep the editor at the top; the extra room stays below
}

}  // namespace pokedex
