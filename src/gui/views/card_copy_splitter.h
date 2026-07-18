#pragma once

#include <QSplitter>
#include <QWidget>

#include "gui/views/splitter_style.h"

namespace pokedex {

// GUI — the shared form ⇄ finder split used by both card-copy pages (Add and Edit):
// the CardCopyForm on the left, the CardFinderPanel on the right, both flexible, with
// the same default sizing and thin divider. Factored out so the two pages stay
// visually consistent — a layout tweak lands in one place. Returns an unparented
// QSplitter; the caller adds it to a layout (which takes ownership).
inline QSplitter* makeCardCopySplitter(QWidget* form, QWidget* finder) {
    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(form);
    splitter->addWidget(finder);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({320, 560});
    thinDivider(splitter);
    return splitter;
}

}  // namespace pokedex
