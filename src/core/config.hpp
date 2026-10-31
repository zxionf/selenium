#pragma once

#include "core/store.hpp"

#include <string>

namespace core::config {

struct Config {
    std::string git_mode{"read"};
    std::string module_scope{"warn"};        // off | warn | block
    std::string message_convention{"auto"};  // auto | sele
    std::string color{"auto"};               // auto | always | never
    int warn_after_skips{3};
    int escalate_after_skips{5};
};

Config load(const store::Paths& paths);
void save(const store::Paths& paths, const Config& config);

} // namespace core::config
