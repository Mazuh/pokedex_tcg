#pragma once

#include <string>

namespace pokedex {

// Returns the greeting shown by the app. Kept Qt-free so it can be
// unit-tested headlessly (no display / QApplication required).
std::string greeting();

}  // namespace pokedex
