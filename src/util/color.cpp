#include "util/color.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace util::color {

bool enabled() {
    static const bool value = [] {
        if (std::getenv("NO_COLOR") != nullptr) return false;
        const char* term = std::getenv("TERM");
        if (term != nullptr && std::string(term) == "dumb") return false;
#if defined(_WIN32)
        return _isatty(_fileno(stdout)) != 0;
#else
        return isatty(STDOUT_FILENO) != 0;
#endif
    }();
    return value;
}

std::string green(const std::string& text) {
    if (!enabled()) return text;
    return "\033[32m" + text + "\033[0m";
}

} // namespace util::color
