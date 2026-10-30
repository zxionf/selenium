#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/config.hpp"
#include "core/entity.hpp"
#include "core/event.hpp"
#include "core/fs.hpp"
#include "core/id.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

void register_init_cmd(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* cmd = app.add_subcommand("init", "Create the .sele layout");
    add_global_opts(cmd, global);
    cmd->callback([&global, &exit_code]() {
        const auto ctx = make_context(global);
        try {
            core::store::ensure_layout(ctx->paths);
            core::config::save(ctx->paths, core::config::load(ctx->paths));
            const bool has_log = core::fsys::exists(ctx->paths.log);
            nlohmann::json result = {{"ok", true},
                                     {"root", ctx->paths.root.string()},
                                     {"sele", ctx->paths.sele.string()},
                                     {"log", has_log}};
            ctx->out.print_both("Initialized " + ctx->paths.sele.string(), result);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });
}
