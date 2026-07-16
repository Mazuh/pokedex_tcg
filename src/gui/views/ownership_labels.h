#pragma once

#include <QString>

#include "core/domain/card_ownership.h"

namespace pokedex {

// GUI — human-facing labels for a CardCopy's ownership state. Kept out of the
// Qt-free core (like region_labels.h / status_labels.h): display wording may
// diverge from — or be localized independently of — the storage token. The switch
// is exhaustive, so a new CardOwnership fails -Wswitch under -Werror.
inline QString ownershipLabel(CardOwnership ownership) {
    switch (ownership) {
        case CardOwnership::Incoming: return QStringLiteral("Incoming");
        case CardOwnership::Owned:    return QStringLiteral("Owned");
        case CardOwnership::Removed:  return QStringLiteral("Removed");
    }
    return QString();
}

}  // namespace pokedex
