#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

#include <optional>
#include <string>

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
struct ScannedCard;

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

    // Pre-fill the printed-identity fields and drive the finder search from a card scan
    // (name-search / "My Cards" add only). Writes the set / number / name onto the form —
    // so the copy is meaningful and pricing can resolve even if the search finds nothing —
    // and runs a finder search by card name so the catalog lists that card's printings for
    // a one-click autofill. Call right after construction, before the page is shown.
    void prefillFrom(const QString& cardName, const QString& setName, const QString& setCode,
                     const QString& collectorNumber);
    // Convenience overload: prefill from a card scan (form baseline + name-search).
    void prefillFrom(const ScannedCard& scanned);

    // Drive the finder's set search from a scan (species-scoped add only). Sets ONLY the
    // finder's "Find by set" query — nothing on the form — so the user picks the printing,
    // which then autofills the whole card deterministically. Call right after construction,
    // before the page is shown. A blank query is a no-op.
    void prefillSetSearch(const QString& setQuery);

    // Paste the read printed-identity fields straight onto the form, with NO catalog
    // search — the "copy to creation form" escape hatch for when the card search is
    // flaky/down. The user reviews/edits and saves; the finder is left untouched (a pick
    // would still override). Language/condition/ownership are left as-is (the default
    // language pre-selection survives). Call right after construction.
    void prefillFormFields(const QString& cardName, const QString& setName,
                           const QString& setCode, const QString& collectorNumber);
    // Convenience overload: paste a card scan's read fields onto the form (no search).
    void prefillFormFields(const ScannedCard& scanned);

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
    void reuseLastComments();                  // copy the last added copy's comment onto the form
    void searchLastSet();                      // point the finder at the last added copy's set
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
    // The language the ctor pre-selected from Settings ("" when none was configured).
    // isDirty() compares against it so our own pre-fill isn't mistaken for user input.
    std::string seededLanguage_;

    CardCopyForm* form_;      // the shared details pane (editable, with a submit action)
    QPushButton* submit_;     // "Add copy" — lives in the form's action row
    QPushButton* uploadButton_;  // "Upload a photo…" — also in the form's action row
    QPushButton* reuseCommentsButton_;  // "Reuse comments from …" — see reuseLastComments()
    // "Search set …" — see searchLastSet(). Null in name-search mode: that finder
    // searches by card name, so pointing it at a set name would search nonsense.
    QPushButton* searchLastSetButton_ = nullptr;
    CardFinderPanel* finder_;  // the shared search field + printings list + preview

    // Session-lived memory of the last successfully added copy, backing the two
    // one-click shortcuts for entering a whole booster: its comment (reuseLastComments)
    // and its set (searchLastSet, which only drives the finder's search). Nothing else
    // is remembered — a click carries over exactly what its button names, never a
    // silent form rewrite. displayName labels the comments button. Static because each
    // add is a brand-new page instance — it must outlive any one page. In-memory only:
    // never persisted, so it resets when the app is closed.
    struct LastAdded {
        bool has = false;
        std::string expansionCode;
        std::string setName;
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
