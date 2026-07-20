#pragma once

#include <QObject>
#include <QStackedWidget>
#include <QString>

#include <functional>
#include <utility>
#include <vector>

#include "core/domain/card_copy.h"
#include "gui/views/card_copy_labels.h"
#include "gui/views/edit_card_copy_page.h"

namespace pokedex {

struct CardBinder;
class CardSearchService;
class CardImageStore;
class CardCopyService;

// GUI — push an EditCardCopyPage onto a section's inner stack and wire its Back to
// tear the page down, then run the host's `onReturn`.
//
// Both copy-mode hosts (the Pokémon browser and a binder guide) open the edit page
// the same way — build the page, add it to the stack, make it current, and on Back
// pop it, dispose it, and refresh-then-reselect. That page-push + teardown boilerplate
// lived duplicated in both PokemonListView::openEditCopy and BinderView::openEditCopy;
// this is its single home. The one part that genuinely differs — how each host
// re-reads its data and restores the selection by identity (the browser lazy-loads
// rows; the guide has them all) — stays with the host as the `onReturn` callback,
// which runs after the page is gone.
inline void pushEditCopyPage(QStackedWidget* stack, CardSearchService& search,
                             CardImageStore& images, CardCopyService& copies,
                             const CardCopy& copy, const std::vector<CardBinder>& binders,
                             std::function<void()> onReturn) {
    auto* page = new EditCardCopyPage(search, images, copies, copy, binders, titleFor(copy));
    QObject::connect(page, &EditCardCopyPage::backRequested, page,
                     [stack, page, onReturn = std::move(onReturn)]() {
                         stack->setCurrentIndex(0);
                         stack->removeWidget(page);
                         page->deleteLater();
                         onReturn();
                     });
    stack->addWidget(page);
    stack->setCurrentWidget(page);
}

}  // namespace pokedex
