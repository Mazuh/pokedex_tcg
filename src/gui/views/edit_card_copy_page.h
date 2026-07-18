#pragma once

#include <QString>
#include <QWidget>

#include <vector>

#include "core/domain/card_copy.h"

class QPushButton;

namespace pokedex {

class CardSearchService;
class CardImageStore;
class CardCopyService;
class CardFinderPanel;
class CardCopyForm;
struct CardBinder;

// GUI — the "Edit card" screen for one owned copy, opened from My Cards. Built from
// the same two shared blocks as the "Add copy" page — CardCopyForm on the left and
// CardFinderPanel on the right — but assembled for editing: the form's identity /
// condition / ownership / binder fields are read-only (they mirror the recorded
// copy), and only the comments box is editable, saved explicitly with "Save
// comments". The recorded fields are shown so the user has context while editing.
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
// emits backRequested() and the host pops + disposes of it. Comments save
// explicitly ("Save comments"); leaving with the box diverged from the record
// prompts to save, discard, or stay rather than dropping the edit silently.
class EditCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search`, `images` and `copies` must outlive this page. `copy` is the copy being
    // edited (its id keys the image and the comment save; its fields fill the form).
    // `binders` populates the read-only binder field. `title` (species · printed
    // identity) personalizes the heading.
    EditCardCopyPage(CardSearchService& search, CardImageStore& images,
                     CardCopyService& copies, CardCopy copy,
                     const std::vector<CardBinder>& binders, const QString& title,
                     QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();

private:
    // Persist the edited comments via CardCopyService::editDetails; true on success.
    bool saveComments();
    void handleBack();      // guard Back on unsaved comments (save/discard/cancel), then leave
    void saveFromFinder();  // persist the picked card's (loaded) preview as the image
    void uploadPhoto();     // pick a local image file and persist it as the image

    CardImageStore& images_;
    CardCopyService& copies_;
    CardCopy copy_;   // the edited copy; comments_ is updated as saves land

    CardCopyForm* form_;
    CardFinderPanel* finder_;
    QPushButton* useButton_;   // "Use this card's image" — enabled once the preview loads
    QPushButton* saveComments_;  // enabled only while the comments differ from the record
};

}  // namespace pokedex
