#pragma once

#include <QString>
#include <QWidget>

#include <optional>

#include "core/domain/types.h"

class QPushButton;

namespace pokedex {

class CardSearchService;
class CardCopyService;
class BinderService;
class CardImageStore;
class CardFinderPanel;
class CardCopyForm;
struct CardCandidate;
struct CardSetInfo;

// GUI — the "add a copy" screen for one Pokémon: the shared CardCopyForm (editable)
// on the left and the shared CardFinderPanel (search + preview) on the right — the
// same two building blocks the "Edit card" page uses, assembled for creation.
// Nothing is fetched on open — a species can have hundreds of printings, so the user
// searches by set (code or name, 3+ chars, debounced) to pull just that set's cards.
// Selecting a card in the finder autofills the form's card reference and shows a
// larger image; the form stays usable by hand, and the page works even when the card
// API is unreachable.
//
// Submitting creates a copy via CardCopyService and, when a card was picked, saves
// its preview image to the workspace (CardImageStore, keyed by the new copy's id) so
// "My Cards" can show it; it then emits copyAdded() (so the host can refresh any
// owned-copy counts) and backRequested() to return to the previous screen. The form
// carries an optional binder picker: when opened unscoped (from the Pokémon browser)
// it defaults to "— None —" and the user may file the copy in any binder; when opened
// from within a binder it is pre-filled with that binder and locked, so the copy lands
// where the user already is. (The remove-with-note flow, and editing an existing
// copy, live elsewhere in OwnedCardsView.)
//
// It is an in-window page pushed onto a host's QStackedWidget (PokemonListView or
// BinderView); a Back button emits backRequested() and the host pops + disposes
// of it, so each open starts fresh.
class AddCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search`, `copies`, `binders` and `cardImages` must outlive this page.
    // `speciesName` is shown in the heading; `dexNumber` drives the printings search
    // and the created copy. `lockedBinder`, when set, pre-fills the binder picker with that binder
    // and locks it (the copy is created there and the user can't repick) — the
    // scoped case, opening from within a binder. When nullopt the picker is a free
    // choice defaulting to "— None —".
    AddCardCopyPage(CardSearchService& search, CardCopyService& copies,
                    BinderService& binders, CardImageStore& cardImages, int dexNumber,
                    const QString& speciesName,
                    std::optional<CardBinderId> lockedBinder = std::nullopt,
                    QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();
    // A copy was persisted; the host should refresh any owned-copy counts it shows.
    void copyAdded();

private:
    void autofillFrom(const CardCandidate& candidate);  // finder pick → the form's card ref
    void chooseSet(const CardSetInfo& set);   // finder set pick → the form's set fields
    void checkUnmatch();                       // drop the finder selection once the form diverges
    void updateSubmitEnabled();                // enable submit once the form is valid
    void submitCopy();                         // create the copy from the form fields

    CardCopyService& copies_;
    CardImageStore& cardImages_;
    int dexNumber_;
    // Set when the page is scoped to a binder: the copy is filed here regardless of
    // the (disabled) combo's display state, so it never lands unfiled even if the
    // binder is absent from the combo (e.g. removed after the guide was opened).
    std::optional<CardBinderId> lockedBinder_;

    CardCopyForm* form_;      // the shared details pane (editable, with a submit action)
    QPushButton* submit_;     // "Add copy" — lives in the form's action row
    CardFinderPanel* finder_;  // the shared search field + printings list + preview
};

}  // namespace pokedex
