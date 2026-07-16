#pragma once

#include <string>

namespace pokedex {

// Trim surrounding ASCII whitespace (" \t\n\r\f\v"); returns empty for an input
// that is empty or all-whitespace. Header-only and dependency-free so any layer
// can use it. Shared by the app services that sanitize user-entered text (binder
// names, wishlist sources, card references) so trimming behaves identically across
// all of them.
inline std::string trim(const std::string& s) {
    const char* ws = " \t\n\r\f\v";
    const auto begin = s.find_first_not_of(ws);
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(ws);
    return s.substr(begin, end - begin + 1);
}

}  // namespace pokedex
