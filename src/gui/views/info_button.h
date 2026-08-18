#pragma once

#include <QObject>
#include <QString>
#include <QToolButton>
#include <QWidget>

#include <functional>
#include <utility>

#include "gui/views/glyph_button.h"
#include "gui/views/info_dialog.h"

namespace pokedex {

// GUI — the shared "ⓘ" affordance: the flat glyph beside a picker or a figure that opens
// the modal InfoDialog explaining it. Every info affordance in the app is built here (the
// card form's Rarity / Condition / Foil pickers, and both price surfaces) so they cannot
// drift on the glyph, the cursor, or what a click does. The shape is makeGlyphButton's, the
// same one the "⚠" hints wear — but only the shape: the ⚠ is amber (applyWarningText),
// since it flags something to look at, while the ⓘ is always there and stays neutral.
//
// The click used to be QToolTip::showText over the same rich text. A tooltip does not
// scroll and Qt clamps it to the screen, so a long explanation (the rarity list runs to 17
// entries) auto-closed unread on a laptop display — hence the dialog, and hence the
// button's own tooltip now carries only the SHORT title.

// The "ⓘ" character itself, exposed so a caller laying out a glyph COLUMN can measure it
// without re-spelling it (card_copy_form sizes one slot for both its glyphs).
inline const QString& infoGlyph() {
    static const QString glyph = QStringLiteral("ⓘ");
    return glyph;
}

// `bodyProvider` is called at CLICK time, not now: the price surfaces rebuild their body
// per render (it carries the shown card's freshness dates). A provider capturing the host's
// `this` is safe — the connection's context object is the returned button, which the caller
// parents as a CHILD of that host, so the button (and with it the functor) is destroyed
// first. Never pass a provider capturing something shorter-lived than the button.
inline QToolButton* makeInfoButton(QWidget* parent, const QString& title,
                                   std::function<QString()> bodyProvider) {
    QToolButton* button = makeGlyphButton(parent, infoGlyph(), title);
    button->setCursor(Qt::PointingHandCursor);  // it opens something; it is not a hover hint
    button->setToolTip(title);                  // the short title only — the body is the dialog
    QObject::connect(button, &QToolButton::clicked, button,
                     [button, title, provider = std::move(bodyProvider)]() {
                         if (!provider) {
                             return;
                         }
                         // Snapshot the body BEFORE the nested event loop: exec() keeps
                         // processing events (a bulk price refresh can re-render the host
                         // underneath us), so the provider must not be consulted across it.
                         const QString body = provider();
                         // Heap + WA_DeleteOnClose, never a stack dialog: exec() runs a
                         // nested loop, and a parented stack object whose parent dies in
                         // that loop is destroyed once by ~QObject and again by the unwind.
                         // Closing deletes this one exactly once, and QDialog::exec guards
                         // its own return against that.
                         //
                         // Parent to the WINDOW, resolved now rather than at construction
                         // (a panel may not be parented yet back then) — it is what a dialog
                         // belongs centered over, and whose screen bounds it must respect.
                         auto* dialog = new InfoDialog(title, body, button->window());
                         dialog->setAttribute(Qt::WA_DeleteOnClose);
                         dialog->exec();  // `dialog` is dangling from here on — do not touch
                     });
    return button;
}

}  // namespace pokedex
