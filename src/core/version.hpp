#pragma once

#include <string>

namespace core {

// Semantic version only, e.g. "0.2.0". Comes from project(VERSION) in CMake.
std::string version_string();

// One-line summary including dependency versions, e.g.
// "sele 0.2.0 (CLI11 2.4.2, nlohmann/json 3.11.2)".
std::string version_display();

} // namespace core
