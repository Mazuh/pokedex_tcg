#include "core/app/card_copy_service.h"

#include <utility>

#include "core/app/uuid.h"
#include "core/storage/card_copy_repository.h"
#include "core/strings.h"

namespace pokedex {

CardCopyService::Clock CardCopyService::systemClock() {
    return [] { return std::chrono::system_clock::now(); };
}

CardCopyService::IdGenerator CardCopyService::uuidGenerator() {
    return [] { return newUuidV4(); };
}

CardCopyService::CardCopyService(CardCopyRepository& repo, Clock clock, IdGenerator idGenerator)
    : repo_(repo), clock_(std::move(clock)), idGenerator_(std::move(idGenerator)) {}

CardCopy CardCopyService::create(std::optional<PokemonDexNum> pokemonDexNum, CardReference cardRef,
                                 CardOwnership ownership, std::optional<CardCondition> condition,
                                 std::optional<CardRarity> rarity, std::optional<CardFoil> foil,
                                 std::optional<CardBinderId> binderId, std::string comments,
                                 std::string externalCardId, bool noFixedPosition) {
    cardRef.expansionCode = trim(cardRef.expansionCode);
    cardRef.language = trim(cardRef.language);
    cardRef.collectorNumber = trim(cardRef.collectorNumber);
    cardRef.setName = trim(cardRef.setName);
    cardRef.name = trim(cardRef.name);
    if (cardRef.collectorNumber.empty()) {
        throw CardCopyError("A card needs a collector number.");
    }

    const Timestamp now = clock_();
    CardCopy copy;
    copy.id = idGenerator_();
    copy.pokemonDexNum = pokemonDexNum;
    copy.cardRef = std::move(cardRef);
    copy.ownership = ownership;
    copy.condition = condition;
    copy.rarity = rarity;
    copy.foil = foil;
    copy.binderId = std::move(binderId);
    copy.comments = std::move(comments);
    copy.externalCardId = trim(externalCardId);
    copy.noFixedPosition = noFixedPosition;
    copy.insertedAt = now;
    copy.updatedAt = now;
    repo_.add(copy);
    ++revision_;
    return copy;
}

CardCopy CardCopyService::require(const CardCopyId& id) {
    std::optional<CardCopy> copy = repo_.find(id);
    if (!copy) {
        throw CardCopyError("No such card copy.");
    }
    return *copy;
}

void CardCopyService::editDetails(const CardCopyId& id, CardReference cardRef,
                                  CardOwnership ownership, std::optional<CardCondition> condition,
                                  std::optional<CardRarity> rarity, std::optional<CardFoil> foil,
                                  std::string comments) {
    cardRef.expansionCode = trim(cardRef.expansionCode);
    cardRef.language = trim(cardRef.language);
    cardRef.collectorNumber = trim(cardRef.collectorNumber);
    cardRef.setName = trim(cardRef.setName);
    cardRef.name = trim(cardRef.name);
    if (cardRef.collectorNumber.empty()) {
        throw CardCopyError("A card needs a collector number.");
    }

    CardCopy copy = require(id);
    if (copy.ownership == CardOwnership::Removed) {
        throw CardCopyError("A removed card is frozen history and cannot be edited.");
    }
    copy.cardRef = std::move(cardRef);
    copy.ownership = ownership;
    copy.condition = condition;
    copy.rarity = rarity;
    copy.foil = foil;
    copy.comments = std::move(comments);
    copy.updatedAt = clock_();
    repo_.update(copy);
    ++revision_;
}

void CardCopyService::assignToBinder(const CardCopyId& id, std::optional<CardBinderId> binderId) {
    CardCopy copy = require(id);
    if (copy.ownership == CardOwnership::Removed) {
        throw CardCopyError("A removed card is frozen history and cannot be filed in a binder.");
    }
    copy.binderId = std::move(binderId);
    copy.updatedAt = clock_();
    repo_.update(copy);
    ++revision_;
}

void CardCopyService::setNoFixedPosition(const CardCopyId& id, bool noFixedPosition) {
    CardCopy copy = require(id);
    if (copy.ownership == CardOwnership::Removed) {
        throw CardCopyError("A removed card is frozen history and cannot be refiled.");
    }
    copy.noFixedPosition = noFixedPosition;
    copy.updatedAt = clock_();
    repo_.update(copy);
    ++revision_;
}

void CardCopyService::linkCatalogCard(const CardCopyId& id, std::string externalCardId) {
    CardCopy copy = require(id);
    copy.externalCardId = trim(externalCardId);
    copy.updatedAt = clock_();
    repo_.update(copy);
    ++revision_;
}

void CardCopyService::remove(const CardCopyId& id, const std::string& note) {
    CardCopy copy = require(id);
    copy.ownership = CardOwnership::Removed;
    if (const std::string trimmedNote = trim(note); !trimmedNote.empty()) {
        // Append on its own line so existing history is preserved.
        copy.comments =
            copy.comments.empty() ? trimmedNote : copy.comments + "\n" + trimmedNote;
    }
    copy.updatedAt = clock_();
    repo_.update(copy);
    ++revision_;
}

void CardCopyService::hardDelete(const CardCopyId& id) {
    require(id);  // surface a missing id as CardCopyError, like the other verbs
    repo_.hardDelete(id);
    ++revision_;
}

std::vector<CardCopy> CardCopyService::listAll() { return repo_.listAll(); }

std::vector<CardCopy> CardCopyService::listByBinder(const CardBinderId& binderId) {
    return repo_.listByBinder(binderId);
}

}  // namespace pokedex
