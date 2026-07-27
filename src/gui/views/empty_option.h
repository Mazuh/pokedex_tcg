#pragma once

#include <QObject>
#include <QString>

namespace pokedex {

// GUI — the single label every editable form dropdown shows for its empty / not-yet-
// picked choice (Condition, Rarity, Foil, Language, Region, Binder). Kept in one place
// so the "nothing chosen" state reads identically everywhere: filling a form, a quick
// eye-scan down the fields tells you exactly what is still blank. This is a *form*
// convention only — read-only views render their own "not set" wording separately.
inline QString noneOptionLabel() { return QObject::tr("— None —"); }

}  // namespace pokedex
