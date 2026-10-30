#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/config.hpp"

void register_config_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* cmd = app.add_subcommand("config", "Show the effective configuration");
    add_global_opts(cmd, global);
    cmd->callback([&global, &exit_code]() {
        const auto ctx = make_context(global);
        (void)exit_code;
        const auto config = core::config::load(ctx->paths);
        ctx->out.print_both(
            "module_scope=" + config.module_scope +
                " message_convention=" + config.message_convention,
            {{"command", "config.show"},
             {"git_mode", config.git_mode},
             {"module_scope", config.module_scope},
             {"message_convention", config.message_convention},
             {"warn_after_skips", config.warn_after_skips},
             {"escalate_after_skips", config.escalate_after_skips}});
    });
}
