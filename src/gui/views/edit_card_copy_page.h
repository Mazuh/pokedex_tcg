#pragma once

#include <QString>
#include <QWidget>

#include <vector>

#include "core/domain/card_binder.h"
#include "core/domain/card_copy.h"

class QLabel;
class QPushButton;
class QStackedWidget;

namespace pokedex {

class CardSearchService;
class CardPriceLookupService;
class CardImageStore;
class CardCopyService;
class CardFinderPanel;
class CardCopyForm;
class CardPricesSummary;

// GUI — the "Edit card" screen for one owned copy, opened from My Cards. Built from
// the same two shared blocks as the "Add copy" page — CardCopyForm on the left and
// CardFinderPanel on the right — but assembled for editing: the printed-identity
// fields are read-only (they mirror the recorded printing, and are visibly muted so
// they read as read-only), while the physical-copy attributes (language, condition,
// ownership), the comments box, and the filing fields (the binder picker and its "no
// fixed position" qualifier) are editable.
//
// EVERY editable field on the form stages and commits together under "Save changes",
// which fans out to the three verbs behind them (CardCopyService::editDetails,
// ::assignToBinder, ::setNoFixedPosition — only the ones that actually changed). The
// binder picker and the checkbox used to write the instant they were touched; that made
// two of a dozen look-alike fields behave unlike the other ten, so a user could tick a
// box, leave with no dirty prompt, and have no way to tell whether it took. Uniformity
// across the form beats per-field cleverness — a new field goes through "Save changes"
// too. The copy's current image is shown as a small thumbnail in the top bar so the user
// always sees what picture is on the card while editing.
//
// The other editable thing is the copy's image, set two ways, both writing to the
// copy's stable workspace path (CardImageStore, keyed by the copy id — overwriting
// any prior image): re-search the catalog and press "Use this card's image" (centered
// under the preview, where the picture it applies to is), or "Upload a photo…" (a
// local file, for a card the catalog doesn't list yet). Setting the image keeps the
// user on the page (the preview shows the picked art) so they can keep editing; a
// toast confirms each save. CardImageStore::imageChanged drives the host to refresh
// its My Cards preview.
//
// It is an in-window page pushed onto OwnedCardsView's inner QStackedWidget; Back
// emits backRequested() and the host pops + disposes of it. The editable fields save
// explicitly ("Save changes"); leaving with any of them diverged from the record
// prompts to save, discard, or stay rather than dropping the edit silently — which is
// exactly the guard the two filing fields were missing while they wrote immediately.
class EditCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search`, `prices`, `images` and `copies` must outlive this page. `copy` is the
    // copy being edited (its id keys the image and the comment save; its fields fill the
    // form; its externalCardId keys the prices panel). `binders` populates the (editable)
    // binder picker, and doubles as the page's baseline for it — a copy filed in a binder
    // absent from this list is treated as unfiled, since that is all the picker can show.
    // `title` (species · printed identity) personalizes the heading.
    EditCardCopyPage(CardSearchService& search, CardPriceLookupService& priceLookup,
                     CardImageStore& images, CardCopyService& copies, CardCopy copy,
                     const std::vector<CardBinder>& binders, const QString& title,
                     QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();

private:
    // Persist every staged field in one gesture: the details (language/condition/
    // ownership/rarity/foil/comments) via CardCopyService::editDetails, plus the binder
    // and the "no fixed position" flag through their own verbs when they changed. True
    // once all of it landed; a failed write reports itself and leaves the rest staged.
    bool saveDetails();
    bool isDirty() const;   // any editable field diverged from the stored record?
    void updateSaveEnabled();  // enable "Save changes" only while isDirty()
    void handleBack();      // guard Back on unsaved edits (save/discard/cancel), then leave
    void saveFromFinder();  // persist the picked card's (loaded) preview as the image
    void uploadPhoto();     // pick a local image file and persist it as the image
    void refreshCurrentImage();  // re-read the copy's stored image into the top-bar thumbnail
    void openPrices();      // push the dedicated prices page onto this page's inner stack

    CardImageStore& images_;
    CardCopyService& copies_;
    CardPriceLookupService& priceLookup_;  // supplies the prices page pushed from here
    CardCopy copy_;   // the edited copy; its fields are updated as saves land

    QStackedWidget* stack_;    // page 0 = the edit content; the prices page pushes over it
    CardCopyForm* form_;
    CardFinderPanel* finder_;
    CardPricesSummary* priceSummary_;  // the copy's read-only market-prices block + Manage button
    QLabel* currentImage_;     // small thumbnail of the copy's current stored image
    QPushButton* useButton_;   // "Use this card's image" — enabled once the preview loads
    QPushButton* saveButton_;  // "Save changes" — enabled only while an edit diverges from the record
};

}  // namespace pokedex
