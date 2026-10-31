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

namespace {

Mode g_mode = Mode::AUTO;

bool tty() {
#if defined(_WIN32)
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::string paint(const char* code, const std::string& text) {
    if (!enabled()) return text;
    return std::string("\033[") + code + "m" + text + "\033[0m";
}

bool contains(const std::string& value, const char* needle) {
    return value.find(needle) != std::string::npos;
}

} // namespace

void configure(Mode mode) { g_mode = mode; }

bool enabled() {
    switch (g_mode) {
    case Mode::NEVER:
        return false;
    case Mode::ALWAYS:
        return true;
    case Mode::AUTO:
        break;
    }
    if (std::getenv("NO_COLOR") != nullptr) return false;
    const char* term = std::getenv("TERM");
    if (term != nullptr && std::string(term) == "dumb") return false;
    return tty();
}

std::string short_status(const std::string& value) {
    if (value == "in_progress") return "active";
    if (value == "pending_review") return "review";
    return value;
}

std::string status(const std::string& value) {
    const std::string text = short_status(value);
    if (value == "done") return paint("32", text);
    if (value == "in_progress") return paint("36", text);
    if (value == "paused" || value == "pending_review" || value == "blocked") {
        return paint("33", text);
    }
    if (value == "failed" || value == "abandoned" || value == "aborted") {
        return paint("31", text);
    }
    if (value == "archived" || value == "skipped") return paint("2", text);
    return text;
}

std::string op(const std::string& value) {
    if (contains(value, ".done") || value == "plan.done" || value == "line.done" ||
        value == "task.done") {
        return paint("32", value);
    }
    if (contains(value, "fail") || contains(value, "abort") || contains(value, "reject")) {
        return paint("31", value);
    }
    if (contains(value, "pause") || contains(value, "block")) return paint("33", value);
    if (contains(value, "start")) return paint("36", value);
    return value;
}

} // namespace util::color
