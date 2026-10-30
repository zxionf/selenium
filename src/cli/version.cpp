#include "cli/common.hpp"

#include "cli/commands.hpp"

#include <sele_version.hpp>

void register_version_cmd(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* cmd = app.add_subcommand("version", "Print the sele version");
    add_global_opts(cmd, global);
    cmd->callback([&global, &exit_code]() {
        const auto ctx = make_context(global);
        (void)exit_code;
        ctx->out.print_both(std::string("sele ") + SELE_VERSION_STRING,
                            {{"version", SELE_VERSION_STRING}});
    });
}
