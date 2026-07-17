#pragma once

#include <QDateTime>
#include <QString>

#include <chrono>

#include "core/domain/types.h"

namespace pokedex {

// GUI — format a UTC Timestamp (the domain's audit stamp) as a local, human-
// readable date-time for display. Kept out of the Qt-free core, like the other
// label helpers. An unset/zero stamp returns empty text, so the caller's cell()
// supplies the em-dash (the single source of that convention — see table_cell.h).
inline QString dateTimeLabel(Timestamp when) {
    const auto secs =
        std::chrono::duration_cast<std::chrono::seconds>(when.time_since_epoch()).count();
    if (secs <= 0) {
        return QString();  // unset — let cell() render the missing-data glyph
    }
    // fromSecsSinceEpoch treats the value as UTC epoch seconds and returns it in the
    // viewer's local time zone.
    return QDateTime::fromSecsSinceEpoch(secs).toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

}  // namespace pokedex
