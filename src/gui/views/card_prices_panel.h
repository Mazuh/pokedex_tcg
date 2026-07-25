#pragma once

#include <QString>
#include <QWidget>

class QLabel;
class QPushButton;
class QToolButton;
class QTableWidget;

namespace pokedex {

class CardPriceLookupService;

// GUI — the reusable "market prices" block for one card, keyed by its external
// catalog id. Shared by the owned-copy surfaces (the Edit page and the My Cards
// detail). It is strictly on-demand: showCard() only renders what is already cached
// (no network); a fetch happens solely when the user clicks Fetch/Refresh, so merely
// viewing a card never hits the API.
//
// States it renders: unlinked (no external id) → a hint; linked-but-unfetched → a
// "Fetch prices" button; has-prices → a headline summary + "as of/fetched" line + a
// Refresh button + an expandable full table (every vendor × variant × metric);
// fetched-but-empty → "no market prices found".
class CardPricesPanel : public QWidget {
    Q_OBJECT

public:
    // `lookup` must outlive this panel.
    explicit CardPricesPanel(CardPriceLookupService& lookup, QWidget* parent = nullptr);

    // Point the panel at a card and render its cached prices. Pass an empty id for an
    // unlinked copy. Never triggers a network fetch.
    void showCard(const QString& externalCardId);

private:
    void render();          // rebuild the UI from the local cache for externalCardId_
    void onFetchClicked();  // the only path that spends a network request

    CardPriceLookupService& lookup_;
    QString externalCardId_;
    bool fetching_ = false;

    QLabel* headline_;
    QLabel* status_;
    QPushButton* fetchButton_;
    QToolButton* toggle_;
    QTableWidget* table_;
};

}  // namespace pokedex
