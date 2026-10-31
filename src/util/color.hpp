#pragma once

#include <string>

namespace util::color {

// True when stdout is a TTY, NO_COLOR is unset, and TERM is not "dumb".
bool enabled();

// Wraps text in ANSI green when colors are enabled; returns text unchanged otherwise.
std::string green(const std::string& text);

} // namespace util::color
