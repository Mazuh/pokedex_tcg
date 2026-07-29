#pragma once

#include <QObject>
#include <QStackedWidget>

#include <functional>
#include <utility>

namespace pokedex {

// GUI — push `page` onto `stack`, make it current, and wire its `backRequested()`
// signal to tear it down (return to page 0, remove it, dispose it) then run the
// host's `onReturn`. This is the single home for the page-push + Back-teardown
// boilerplate every in-window sub-page repeats (add copy, edit copy, prices,
// wishlist, binder edit) — factoring it here keeps the teardown order and the
// deleteLater() from drifting between call sites.
//
// `Page` must expose a `void backRequested()` signal. The specialized
// pushEditCopyPage / pushPricesPage helpers layer their extra wiring on top of the
// same shape; a plain page (like BinderEditPage) uses this directly.
template <typename Page>
void pushBackablePage(QStackedWidget* stack, Page* page, std::function<void()> onReturn) {
    QObject::connect(page, &Page::backRequested, page,
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
