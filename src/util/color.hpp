#pragma once

#include <string>

namespace util::color {

enum class Mode {
    AUTO,   // TTY + NO_COLOR/TERM checks
    ALWAYS, // force on
    NEVER   // force off
};

void configure(Mode mode);
bool enabled();

// Colors the value according to its workflow status; unknown values pass through.
std::string status(const std::string& value);

// Colors an event op by prefix (done=green, fail/abort=red, pause/block=yellow, start=cyan).
std::string op(const std::string& value);

} // namespace util::color
