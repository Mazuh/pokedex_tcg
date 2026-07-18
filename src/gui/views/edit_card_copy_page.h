#pragma once

#include <QString>
#include <QWidget>

#include "core/domain/types.h"

class QPushButton;

namespace pokedex {

class CardSearchService;
class CardImageStore;
class CardFinderPanel;

// GUI — the "Edit card" screen for one owned copy, opened from My Cards. This is the
// app's first per-copy edit surface; for now it edits only the copy's image, the
// natural first thing to change (a copy added before images were persisted, or one
// too new for the catalog that was recorded by hand, shows a placeholder until set
// here). Condition / comments / reference editing are a later step that would land
// on this same page.
//
// Two ways to set the image, both writing to the copy's stable workspace path
// (CardImageStore, keyed by the copy id — so it overwrites any prior image):
//   • re-search the card catalog with the SAME finder used at creation
//     (CardFinderPanel) and use the picked printing's art; or
//   • upload a local photo file (for a card the catalog doesn't list yet).
// On either, it saves via CardImageStore (whose imageChanged() signal the host uses
// to refresh its preview) then emits backRequested() to pop back to the list. "Use
// this card's image" stays disabled until the picked card's image has actually
// loaded, so the save is synchronous and the host's refresh shows the new art.
//
// It is an in-window page pushed onto OwnedCardsView's inner QStackedWidget; a Back
// button emits backRequested() and the host pops + disposes of it.
class EditCardCopyPage : public QWidget {
    Q_OBJECT

public:
    // `search` and `images` must outlive this page. `copyId` is the copy whose image
    // is being set; `dexNumber` scopes the finder's search to that copy's species;
    // `title` (species · printed identity) personalizes the heading.
    EditCardCopyPage(CardSearchService& search, CardImageStore& images, CardCopyId copyId,
                     int dexNumber, const QString& title, QWidget* parent = nullptr);

Q_SIGNALS:
    void backRequested();

private:
    void saveFromFinder();  // persist the picked card's (loaded) preview, then return
    void uploadPhoto();     // pick a local image file, persist it, then return

    CardImageStore& images_;
    CardCopyId copyId_;

    CardFinderPanel* finder_;
    QPushButton* useButton_;  // "Use this card's image" — enabled once the preview loads
};

}  // namespace pokedex
