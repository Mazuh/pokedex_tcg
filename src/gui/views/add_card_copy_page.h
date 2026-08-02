#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

#include <optional>
#include <string>

#include "core/domain/card_condition.h"
#include "core/domain/types.h"

class QEvent;
class QLabel;
class QObject;
class QPushButton;
class QStackedWidget;

namespace pokedex {

class CardSearchService;
class CardCopyService;
class CardPriceLookupService;
class BinderService;
class CardImageStore;
class CardFinderPanel;
class CardCopyForm;
struct CardCandidate;
struct CardSetInfo;

// GUI — the "add a copy" screen: the shared CardCopyForm (editable) on the left and
// the shared CardFinderPanel (search + preview) on the right — the same two building
// blocks the "Edit card" page uses, assembled for creation. It serves two cases,
// keyed by whether `dexNumber` is set: scoped to a Pokémon species (opened from the
// Pokémon browser or a binder — the finder searches that species' printings), or
// species-free (opened from "My Cards" — for a Trainer/Energy card that depicts no
// species, the finder searches by card name instead).
// Nothing is fetched on open — a species can have hundreds of printings, so the user
// searches by set (code or name, 3+ chars, debounced) to pull just that set's cards.
// Selecting a card in the finder autofills the form's card reference and shows a
// larger image; the form stays usable by hand, and the page works even when the card
// API is unreachable.
//
// Instead of the catalog image, the user may upload a photo of the card in hand
// ("Upload a photo…"): the chosen image replaces the search pane entirely (a preview
// with a "Remove photo" button that swaps the finder back) and is held in memory —
// unlike the Edit page, which writes to disk immediately, nothing is persisted until
// "Add copy", so an abandoned add leaves no file. An uploaded photo takes precedence
// over any finder pick when the copy is saved.
//
// Submitting creates a copy via CardCopyService and, when a card was picked (or a
// photo uploaded), saves its image to the workspace (CardImageStore, keyed by the new
// copy's id) so "My Cards" can show it; it then emits copyAdded() (so the host can
// refresh any owned-copy counts) and backRequested() to return to the previous screen. The form
// carries an optional binder picker: when opened unscoped (from the Pokémon browser)
// it defaults to "— None —" and the user may file the copy in any binder; when opened
// from within a binder it is pre-filled with that binder and locked, so the copy lands
// where the user already is. (The remove-with-note flow, and editing an existing
// copy, live elsewhere in OwnedCardsView.)
//
// It is an in-window page pushed onto a host's QStackedWidget (PokemonListView or
// BinderView); a Back button emits backRequested() and the host pops + disposes
// of it, so each open starts fresh. Leaving with a partly-filled form (the user
// started composing a copy but never pressed "Add copy") prompts to discard rather
// than dropping the work silently — the same courtesy the Edit page gives comments.
class AddCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search`, `copies`, `priceLookup`, `binders` and `cardImages` must outlive this
    // page. `priceLookup` is the app-wide price transport: on a successful add the page
    // kicks off a best-effort background price fetch for the new copy (resolve → link →
    // fetch), so the user no longer has to press Fetch by hand after every add.
    // `dexNumber` set → scoped to that species (its printings drive the finder and the
    // created copy names the species); nullopt → species-free (the finder searches by
    // card name and the copy depicts no Pokémon). `speciesName` is shown in the heading
    // in the scoped case (leave blank when species-free). `lockedBinder`, when set,
    // pre-fills the binder picker with that binder and locks it (the copy is created
    // there and the user can't repick) — opening from within a binder. When nullopt the
    // picker is a free choice defaulting to "— None —".
    AddCardCopyPage(CardSearchService& search, CardCopyService& copies,
                    CardPriceLookupService& priceLookup, BinderService& binders,
                    CardImageStore& cardImages, std::optional<PokemonDexNum> dexNumber,
                    const QString& speciesName,
                    std::optional<CardBinderId> lockedBinder = std::nullopt,
                    QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();
    // A copy was persisted; the host should refresh any owned-copy counts it shows.
    void copyAdded();

protected:
    // Rescales the uploaded-photo preview when its label resizes (window/splitter
    // drag, or the photo page first becoming current) — same idiom as CardFinderPanel.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void autofillFrom(const CardCandidate& candidate);  // finder pick → the form's card ref
    void chooseSet(const CardSetInfo& set);   // finder set pick → the form's set fields
    void checkUnmatch();                       // drop the finder selection once the form diverges
    void updateSubmitEnabled();                // enable submit once the form is valid
    void submitCopy();                         // create the copy from the form fields
    void reuseLastFields();                    // fill set + comments from the last added copy
    void uploadPhoto();                        // pick a local image → hold it, replace the search
    void removeUploadedPhoto();                // drop the held photo → the finder returns
    void refreshUploadedPreview();             // render the held photo into the preview pane
    void handleBack();                         // guard Back on a partly-filled form, then leave
    bool isDirty() const;                      // has the user entered anything worth confirming?

    CardCopyService& copies_;
    CardPriceLookupService& priceLookup_;  // app-wide price transport for the auto-fetch on add
    CardImageStore& cardImages_;
    std::optional<PokemonDexNum> dexNumber_;  // nullopt → species-free card
    QString speciesName_;  // for the success toast, which shows after the page is gone
    // Set when the page is scoped to a binder: the copy is filed here regardless of
    // the (disabled) combo's display state, so it never lands unfiled even if the
    // binder is absent from the combo (e.g. removed after the guide was opened).
    std::optional<CardBinderId> lockedBinder_;

    CardCopyForm* form_;      // the shared details pane (editable, with a submit action)
    QPushButton* submit_;     // "Add copy" — lives in the form's action row
    QPushButton* uploadButton_;  // "Upload a photo…" — also in the form's action row
    QPushButton* reuseButton_;   // "Reuse last info from …" — see reuseLastFields()
    CardFinderPanel* finder_;  // the shared search field + printings list + preview

    // Session-lived memory of the last successfully added copy, so opening a fresh add
    // page for the next card from the same booster can prefill in one click (the "Reuse
    // last info from …" button — see reuseLastFields). We keep the set (to drive the
    // finder search) plus the physical attributes the card search can't supply
    // (language, condition) and the note; displayName labels the button. Static because
    // each add is a brand-new page instance — it must outlive any one page. In-memory
    // only: never persisted, so it resets when the app is closed.
    struct LastAdded {
        bool has = false;
        std::string expansionCode;
        std::string setName;
        std::string language;
        std::optional<CardCondition> condition;
        std::string comments;
        QString displayName;  // the last card/species name, for the button label
    };
    static LastAdded lastAdded_;

    // The right side of the split is a stack: the finder (search + preview) on index 0,
    // and the uploaded-photo preview on index 1. Uploading a photo swaps to index 1
    // ("replaces the search"); "Remove photo" swaps back. The photo is held here and
    // only written to CardImageStore at submit — null means "no upload, use the finder".
    QStackedWidget* rightStack_;
    QLabel* uploadedPreview_;  // renders uploadedImage_ on the stack's photo page
    QPixmap uploadedImage_;    // the held upload (null = none); wins over the finder on submit
};

}  // namespace pokedex
