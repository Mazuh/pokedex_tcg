#pragma once

#include <QObject>
#include <QPushButton>
#include <QStyle>
#include <QWidget>

namespace pokedex {

// GUI — a screen's "Back" navigation control: a labeled QPushButton with the
// platform back-arrow icon that is explicitly NOT the window's default button.
// Without setAutoDefault/setDefault(false) it renders as the big blue macOS
// default button and grabs the default on focus. Centralized (like table_cell.h
// and splitter_style.h) so every in-window Back button is built identically and
// a new one can't silently reintroduce that defect. The caller wires clicked().
inline QPushButton* makeBackButton(QWidget* parent) {
    auto* button = new QPushButton(QObject::tr("Back"), parent);
    button->setIcon(parent->style()->standardIcon(QStyle::SP_ArrowBack));
    button->setAutoDefault(false);
    button->setDefault(false);
    return button;
}

}  // namespace pokedex
