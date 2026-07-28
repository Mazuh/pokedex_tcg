#pragma once

#include <QString>
#include <QWidget>

#include "core/domain/card_copy.h"

namespace pokedex {

class CardPriceLookupService;
class CardCopyService;

// GUI — the per-copy pricing-management screen: a full in-window page dedicated to one card's
// market prices, hosting the interactive CardPricesPanel (Fetch/Refresh, Clear, hide/restore a
// vendor) under a Back top bar and a card heading. It exists so the card inspector doesn't have
// to carry those controls in its tiny pane — the inspector now shows the read-only
// CardPricesSummary (figures + links + ⓘ) and a "Manage prices" button that pushes this page,
// exactly the move the wishlist made with WishlistEditPage.
//
// Like the Add/Edit copy pages it is pushed onto a host's QStackedWidget and its Back button
// emits backRequested() so the host pops + disposes it. A Fetch here can invisibly resolve and
// persist the copy's tcgdex link; that is relayed up as cardLinked() so the host updates its
// cached copy (otherwise a re-selection would re-run the resolve and value stats keyed on the
// external id would miss it). It is the single home for pricing verbs, so future price actions
// land here rather than back in the cramped inspector.
class PricesEditPage : public QWidget {
    Q_OBJECT

public:
    // `lookup` and `copies` must outlive this page. `copy` is the card whose prices are managed;
    // its label + printed identity fill the heading and it seeds the panel.
    PricesEditPage(CardPriceLookupService& lookup, CardCopyService& copies, const CardCopy& copy,
                   QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();
    // A Fetch resolved and persisted this copy's tcgdex link. Forwarded from the hosted panel so
    // the owning view can update its cached copy.
    void cardLinked(const QString& copyId, const QString& externalCardId);
};

}  // namespace pokedex
