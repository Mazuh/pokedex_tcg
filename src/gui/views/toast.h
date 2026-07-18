#pragma once

#include <QString>

class QWidget;

namespace pokedex {

// GUI — a lightweight, non-modal confirmation "toast": a translucent pill that
// appears near the bottom of `anchor`'s top-level window, holds for a couple of
// seconds, then fades out. Use it to confirm a successful write (a copy added, a
// binder renamed, an image saved) — especially where the action also navigates
// away, since the toast is parented to the window and outlives the page that
// triggered it. It never intercepts input and disposes of itself.
//
// This is the "it worked" channel only. Failures still use a modal QMessageBox so
// the user must acknowledge them; a toast is deliberately easy to miss.
void showToast(QWidget* anchor, const QString& message);

}  // namespace pokedex
