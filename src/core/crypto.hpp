#pragma once

#include <string>

namespace remin::core {

/// Compute SHA256 hash of input string and return as hex string.
std::string sha256_hex(const std::string& input);

} // namespace remin::core