#pragma once

#include <QObject>
#include <QStackedWidget>
#include <QString>

#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/domain/card_copy.h"
#include "core/domain/types.h"
#include "gui/views/owned_copy_buckets.h"  // findOwnedCopy
#include "gui/views/prices_edit_page.h"

namespace pokedex {

class CardPriceLookupService;
class CardCopyService;

// GUI — push a PricesEditPage onto a section's inner stack and wire it up: forward its
// Fetch-driven cardLinked to `onLinked` (the host writes the id back into its cached copy), and
// on Back tear the page down and run `onReturn`. Mirrors pushEditCopyPage — the same page-push +
// teardown boilerplate every host that manages prices needs (the two species hosts, My Cards,
// and the Edit page's own inner stack), so it lives in one place. Back returns to index 0, which
// is the browse/detail page for a section stack and the edit content for the Edit page's stack.
inline void pushPricesPage(QStackedWidget* stack, CardPriceLookupService& lookup,
                           CardCopyService& copies, const CardCopy& copy,
                           std::function<void(const QString&, const QString&)> onLinked,
                           std::function<void()> onReturn) {
    auto* page = new PricesEditPage(lookup, copies, copy);
    if (onLinked) {
        QObject::connect(page, &PricesEditPage::cardLinked, page,
                         [onLinked = std::move(onLinked)](const QString& copyId,
                                                          const QString& externalCardId) {
                             onLinked(copyId, externalCardId);
                         });
    }
    QObject::connect(page, &PricesEditPage::backRequested, page,
                     [stack, page, onReturn = std::move(onReturn)]() {
                         stack->setCurrentIndex(0);
                         stack->removeWidget(page);
                         page->deleteLater();
                         if (onReturn) {
                             onReturn();
                         }
                     });
    stack->addWidget(page);
    stack->setCurrentWidget(page);
}

// GUI — the guarded prices-page open for a SPECIES-INDEXED host: locate the shown copy
// (`copyId` under species `dex`) in the host's bucketed-by-dex map and, if it's still there,
// push its prices page. A no-op when the copy is gone. Mirrors openEditCopyFromBuckets, and
// like it has one caller today, the Pokémon browser. A host whose row IS a card (My Cards,
// the binder guide) finds its copy in a flat list and calls pushPricesPage directly — which
// it MUST, since these buckets are species-keyed and Owned-only and would drop precisely the
// species-free and Incoming rows such a surface exists to show.
inline void openPricesFromBuckets(
    QStackedWidget* stack, CardPriceLookupService& lookup, CardCopyService& copies,
    const std::unordered_map<PokemonDexNum, std::vector<CardCopy>>& byDex, int dex,
    const QString& copyId, std::function<void(const QString&, const QString&)> onLinked,
    std::function<void()> onReturn) {
    const CardCopy* copy = findOwnedCopy(byDex, dex, copyId);
    if (!copy) {
        return;
    }
    pushPricesPage(stack, lookup, copies, *copy, std::move(onLinked), std::move(onReturn));
}

}  // namespace pokedex
