#include "core/storage/wishlist_repository.h"

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

void WishlistRepository::save(const Wishlist& wishlist) {
    // The parent row and its source rows are one logical unit spread across several
    // statements, so they run in a transaction. Without it, a failure mid-sequence
    // could leave a committed parent with a stale source set.
    db_.transaction([&] {
        // Upsert the parent: a brand-new species takes the caller's insertedAt;
        // an existing one keeps its original insertedAt and only bumps updatedAt.
        Statement row(db_,
                      "INSERT INTO wishlist(pokemon_dex_num, inserted_at, updated_at)"
                      " VALUES(?, ?, ?)"
                      " ON CONFLICT(pokemon_dex_num) DO UPDATE SET"
                      " updated_at = excluded.updated_at;");
        row.bindInt(1, wishlist.pokemonDexNum);
        row.bindText(2, timestampToIso(wishlist.insertedAt));
        row.bindText(3, timestampToIso(wishlist.updatedAt));
        row.step();

        // Replace the whole source set: clear the old rows, then re-insert.
        Statement clear(db_, "DELETE FROM wishlist_source WHERE pokemon_dex_num = ?;");
        clear.bindInt(1, wishlist.pokemonDexNum);
        clear.step();

        for (const std::string& source : wishlist.sources) {
            Statement src(db_,
                          "INSERT INTO wishlist_source(pokemon_dex_num, source)"
                          " VALUES(?, ?);");
            src.bindInt(1, wishlist.pokemonDexNum);
            src.bindText(2, source);
            src.step();
        }
    });
}

std::optional<Wishlist> WishlistRepository::find(PokemonDexNum pokemonDexNum) {
    Statement row(db_,
                  "SELECT inserted_at, updated_at FROM wishlist"
                  " WHERE pokemon_dex_num = ?;");
    row.bindInt(1, pokemonDexNum);
    if (!row.step()) {
        return std::nullopt;
    }

    Wishlist wishlist;
    wishlist.pokemonDexNum = pokemonDexNum;
    wishlist.insertedAt = timestampFromIso(row.columnText(0));
    wishlist.updatedAt = timestampFromIso(row.columnText(1));

    Statement sources(db_,
                      "SELECT source FROM wishlist_source"
                      " WHERE pokemon_dex_num = ? ORDER BY source;");
    sources.bindInt(1, pokemonDexNum);
    while (sources.step()) {
        wishlist.sources.insert(sources.columnText(0));
    }
    return wishlist;
}

std::vector<Wishlist> WishlistRepository::listAll() {
    // One JOIN over parents and sources, ordered by dex then source, so the whole
    // list builds in a single pass. The INNER JOIN drops source-less parents (they
    // render nothing); the dex ordering lets consecutive rows group into one
    // Wishlist each.
    Statement stmt(db_,
                   "SELECT w.pokemon_dex_num, w.inserted_at, w.updated_at, s.source"
                   " FROM wishlist w"
                   " JOIN wishlist_source s ON s.pokemon_dex_num = w.pokemon_dex_num"
                   " ORDER BY w.pokemon_dex_num, s.source;");
    std::vector<Wishlist> wishlists;
    while (stmt.step()) {
        const PokemonDexNum dex = stmt.columnInt(0);
        if (wishlists.empty() || wishlists.back().pokemonDexNum != dex) {
            Wishlist wishlist;
            wishlist.pokemonDexNum = dex;
            wishlist.insertedAt = timestampFromIso(stmt.columnText(1));
            wishlist.updatedAt = timestampFromIso(stmt.columnText(2));
            wishlists.push_back(std::move(wishlist));
        }
        wishlists.back().sources.insert(stmt.columnText(3));
    }
    return wishlists;
}

void WishlistRepository::remove(PokemonDexNum pokemonDexNum) {
    // Source rows cascade via wishlist_source's ON DELETE CASCADE.
    Statement stmt(db_, "DELETE FROM wishlist WHERE pokemon_dex_num = ?;");
    stmt.bindInt(1, pokemonDexNum);
    stmt.step();
}

std::vector<PokemonDexNum> WishlistRepository::wishedDexNums() {
    Statement stmt(db_, "SELECT DISTINCT pokemon_dex_num FROM wishlist_source;");
    std::vector<PokemonDexNum> dexNums;
    while (stmt.step()) {
        dexNums.push_back(stmt.columnInt(0));
    }
    return dexNums;
}

}  // namespace pokedex
