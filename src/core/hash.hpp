#pragma once

#include <string>
#include <string_view>

namespace core::hash {

// Lowercase hex SHA-256 of the given bytes. Used to freeze goal/acceptance
// text at line open time.
std::string sha256_hex(std::string_view data);

} // namespace core::hash
