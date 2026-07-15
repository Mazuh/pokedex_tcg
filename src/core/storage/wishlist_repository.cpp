#include "core/storage/wishlist_repository.h"

#include "core/storage/codecs.h"
#include "core/storage/database.h"
#include "core/storage/statement.h"

namespace pokedex {

void WishlistRepository::add(const Wishlist& wishlist) {
    // The wishlist row and its source rows are one logical unit spread across
    // several statements, so they run in a transaction. Without it, a failure
    // after the first INSERT would leave a committed wishlist row that then
    // blocks (via its primary key) any retry for that species. Mirror migrate()'s
    // best-effort ROLLBACK on failure.
    db_.exec("BEGIN;");
    try {
        Statement row(db_,
                      "INSERT INTO wishlist(pokemon_dex_num, inserted_at, updated_at)"
                      " VALUES(?, ?, ?);");
        row.bindInt(1, wishlist.pokemonDexNum);
        row.bindText(2, timestampToIso(wishlist.insertedAt));
        row.bindText(3, timestampToIso(wishlist.updatedAt));
        row.step();

        for (const std::string& source : wishlist.sources) {
            Statement src(db_,
                          "INSERT INTO wishlist_source(pokemon_dex_num, source)"
                          " VALUES(?, ?);");
            src.bindInt(1, wishlist.pokemonDexNum);
            src.bindText(2, source);
            src.step();
        }
    } catch (...) {
        try {
            db_.exec("ROLLBACK;");
        } catch (...) {
            // The transaction is already doomed; surface the original failure.
        }
        throw;
    }
    db_.exec("COMMIT;");
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
