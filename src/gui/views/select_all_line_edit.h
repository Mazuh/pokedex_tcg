#pragma once

#include <QFocusEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <Qt>

namespace pokedex {

// GUI — a QLineEdit that selects its whole contents whenever it gains focus, so
// the next keystroke replaces the text with no manual Cmd+A. Used for the app's
// search bars, where a session means many back-to-back searches. Declares no new
// signals/slots, so it needs no Q_OBJECT and stays header-only (like back_button.h).
//
// The Qt subtlety: selectAll() in focusInEvent works for keyboard/tab focus, but a
// mouse click's subsequent release repositions the cursor and clears the selection.
// So for mouse focus we defer the select to mouseReleaseEvent; other focus reasons
// select immediately. On release we only select-all for a plain click — if the
// focusing click was a drag, the user already picked a substring, so we leave it.
class SelectAllLineEdit : public QLineEdit {
public:
    using QLineEdit::QLineEdit;  // inherit ctors (QWidget* parent, etc.)

protected:
    void focusInEvent(QFocusEvent* event) override {
        QLineEdit::focusInEvent(event);
        if (event->reason() == Qt::MouseFocusReason) {
            selectOnRelease_ = true;  // a click will clear it; wait for release
        } else {
            selectAll();  // tab / shortcut / programmatic focus
        }
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        QLineEdit::mouseReleaseEvent(event);
        if (selectOnRelease_) {
            selectOnRelease_ = false;
            if (!hasSelectedText()) {  // a plain click, not a drag-select
                selectAll();
            }
        }
    }

private:
    bool selectOnRelease_ = false;
};

}  // namespace pokedex
