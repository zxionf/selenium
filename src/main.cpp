#include "cli/commands.hpp"
#include "cli/common.hpp"

#include <CLI/CLI.hpp>
#include <sele_version.hpp>

int main(int argc, char** argv) {
    CLI::App app{"sele — constraint-first agent task ledger"};
    app.set_version_flag("--version", SELE_VERSION_STRING);

    GlobalOptions global;
    add_global_opts(&app, global);

    int exit_code = 0;
    register_init_cmd(app, global, exit_code);
    register_version_cmd(app, global, exit_code);
    register_plan_cmds(app, global, exit_code);
    register_line_cmds(app, global, exit_code);
    register_task_cmds(app, global, exit_code);
    register_step_cmds(app, global, exit_code);
    register_query_cmds(app, global, exit_code);
    register_git_cmds(app, global, exit_code);
    register_cache_cmds(app, global, exit_code);
    register_config_cmds(app, global, exit_code);

    app.require_subcommand(1);
    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& error) {
        return app.exit(error);
    }
    return exit_code;
}
