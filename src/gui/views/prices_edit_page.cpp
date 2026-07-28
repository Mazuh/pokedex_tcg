#include "gui/views/prices_edit_page.h"

#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "gui/views/back_button.h"
#include "gui/views/card_copy_labels.h"  // speciesOrCardName, collectorLine
#include "gui/views/card_prices_panel.h"

namespace pokedex {

PricesEditPage::PricesEditPage(CardPriceLookupService& lookup, CardCopyService& copies,
                               const CardCopy& copy, QWidget* parent)
    : QWidget(parent) {
    auto* backButton = makeBackButton(this);
    connect(backButton, &QPushButton::clicked, this, &PricesEditPage::backRequested);

    // The card's label headlines the screen; the printed identity sits under it as a muted
    // subtitle, so the two together name the exact printing without repeating it in the panel.
    const QString label = speciesOrCardName(copy);
    auto* heading = new QLabel(label.isEmpty() ? tr("Card prices") : label, this);
    QFont headingFont = heading->font();
    headingFont.setBold(true);
    headingFont.setPointSize(headingFont.pointSize() + 3);
    heading->setFont(headingFont);

    auto* topBar = new QHBoxLayout;
    topBar->addWidget(backButton);
    topBar->addWidget(heading);
    topBar->addStretch();

    const QString identity = collectorLine(copy.cardRef);
    auto* subtitle = new QLabel(
        identity.isEmpty() ? tr("Market prices for this card.") : identity, this);
    subtitle->setEnabled(false);  // muted: a hint, not content

    // The interactive prices block — the same reusable panel the inspector used to embed, now
    // with room for its Fetch/Refresh, Clear, and per-vendor hide/restore controls (and future
    // pricing actions). Its Fetch-driven auto-link is relayed up so the host stays in sync.
    auto* panel = new CardPricesPanel(lookup, copies, this);
    connect(panel, &CardPricesPanel::cardLinked, this, &PricesEditPage::cardLinked);
    panel->showCopy(copy);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->addLayout(topBar);
    layout->addWidget(subtitle);
    layout->addWidget(panel);
    layout->addStretch();  // keep the panel at the top; the extra room stays below
}

}  // namespace pokedex
