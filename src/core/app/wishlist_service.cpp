#include "core/app/wishlist_service.h"

#include <algorithm>
#include <span>
#include <utility>

#include "core/domain/pokemon_catalog.h"
#include "core/storage/wishlist_repository.h"
#include "core/strings.h"

namespace pokedex {

namespace {

std::string requireSource(const std::string& raw) {
    std::string source = trim(raw);
    if (source.empty()) {
        throw WishlistError("A wishlist source cannot be blank.");
    }
    return source;
}

}  // namespace

WishlistService::Clock WishlistService::systemClock() {
    return [] { return std::chrono::system_clock::now(); };
}

WishlistService::WishlistService(WishlistRepository& repo, Clock clock)
    : repo_(repo), clock_(std::move(clock)) {}

std::optional<Wishlist> WishlistService::forPokemon(PokemonDexNum pokemonDexNum) {
    return repo_.find(pokemonDexNum);
}

std::vector<WishlistEntry> WishlistService::listAll() {
    const std::span<const Pokemon> catalog = pokemonCatalog();

    std::vector<WishlistEntry> entries;
    for (const Wishlist& wishlist : repo_.listAll()) {
        WishlistEntry entry;
        // The catalog is contiguous 1..N in dex order, so index directly; guard
        // against an out-of-range dex number rather than trusting the row blindly.
        const auto index = static_cast<std::size_t>(wishlist.pokemonDexNum - 1);
        if (index >= catalog.size()) {
            continue;
        }
        entry.pokemon = catalog[index];
        entry.sources.assign(wishlist.sources.begin(), wishlist.sources.end());
        entry.insertedAt = wishlist.insertedAt;
        entry.updatedAt = wishlist.updatedAt;
        entries.push_back(std::move(entry));
    }
    // Most recently updated first (the shopping-list order); tiebreak by newest
    // insertion, then dex number for a stable ordering.
    std::sort(entries.begin(), entries.end(), [](const WishlistEntry& a, const WishlistEntry& b) {
        if (a.updatedAt != b.updatedAt) {
            return a.updatedAt > b.updatedAt;
        }
        if (a.insertedAt != b.insertedAt) {
            return a.insertedAt > b.insertedAt;
        }
        return a.pokemon.dexNumber < b.pokemon.dexNumber;
    });
    return entries;
}

void WishlistService::addSource(PokemonDexNum pokemonDexNum, std::string source) {
    const std::string clean = requireSource(source);
    const Timestamp now = clock_();

    std::optional<Wishlist> existing = repo_.find(pokemonDexNum);
    Wishlist wishlist = existing.value_or(Wishlist{pokemonDexNum, {}, now, now});
    wishlist.sources.insert(clean);
    wishlist.updatedAt = now;
    repo_.save(wishlist);
}

void WishlistService::editSource(PokemonDexNum pokemonDexNum,
                                 const std::string& oldSource, std::string newSource) {
    const std::string clean = requireSource(newSource);

    std::optional<Wishlist> existing = repo_.find(pokemonDexNum);
    if (!existing) {
        return;
    }
    // In-place replace: only substitute when oldSource is actually present.
    // Erasing an absent value then inserting would fabricate a phantom extra
    // source (a stale edit acting on an out-of-date view), so bail if nothing
    // was removed.
    if (existing->sources.erase(oldSource) == 0) {
        return;
    }
    existing->sources.insert(clean);
    existing->updatedAt = clock_();
    repo_.save(*existing);
}

void WishlistService::removeSource(PokemonDexNum pokemonDexNum,
                                   const std::string& source) {
    std::optional<Wishlist> existing = repo_.find(pokemonDexNum);
    if (!existing) {
        return;
    }
    existing->sources.erase(source);
    if (existing->sources.empty()) {
        // Don't leave a sourceless parent row lingering (it renders nothing).
        repo_.remove(pokemonDexNum);
        return;
    }
    existing->updatedAt = clock_();
    repo_.save(*existing);
}

}  // namespace pokedex
