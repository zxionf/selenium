#include "core/config.hpp"

#include "core/fs.hpp"

#include <nlohmann/json.hpp>

namespace core::config {

Config load(const store::Paths& paths) {
    Config config;
    if (!core::fsys::exists(paths.config)) return config;
    try {
        const auto value = nlohmann::json::parse(core::fsys::read_file(paths.config));
        config.git_mode = value.value("git_mode", config.git_mode);
        config.module_scope = value.value("module_scope", config.module_scope);
        config.message_convention = value.value("message_convention", config.message_convention);
        config.color = value.value("color", config.color);
        config.warn_after_skips = value.value("warn_after_skips", config.warn_after_skips);
        config.escalate_after_skips =
            value.value("escalate_after_skips", config.escalate_after_skips);
    } catch (...) {
    }
    return config;
}

void save(const store::Paths& paths, const Config& config) {
    core::fsys::write_atomic(
        paths.config,
        nlohmann::json({{"version", 2},
                        {"git_mode", config.git_mode},
                        {"module_scope", config.module_scope},
                        {"message_convention", config.message_convention},
                        {"color", config.color},
                        {"warn_after_skips", config.warn_after_skips},
                        {"escalate_after_skips", config.escalate_after_skips}})
                .dump(2) +
            "\n");
}

} // namespace core::config
