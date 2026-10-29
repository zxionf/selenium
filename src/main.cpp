#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <string>

#include "cli/commands.hpp"
#include "core/version.hpp"
#include "thirdparty/deps.hpp"
#include "util/output.hpp"
#include "util/exit_codes.hpp"

namespace {

// CLI11 may reject arguments before `--output` is parsed (for example when a
// required option is missing). Scan argv so error output still honours the
// requested format in that case.
bool wants_json(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) return std::string(argv[i + 1]) == "json";
        if (arg.rfind("--output=", 0) == 0) return arg.substr(9) == "json";
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"sele - line/task orchestration for agents (.sele) and legacy .design/.agent projects"};
    app.require_subcommand(1);
    app.set_version_flag("--version", &core::version_display, "Show version information and exit");

    GlobalOptions global_opts;
    int exit_code = exit_codes::SUCCESS;

    app.add_option("--root", global_opts.root, "Project root directory (default: current dir, search upward for .sele/ or .design/)");
    app.add_option("--output", global_opts.output_format, "Output format: text or json")
        ->transform(CLI::CheckedTransformer(std::map<std::string, util::Output::Format>{
            {"text", util::Output::Format::TEXT},
            {"json", util::Output::Format::JSON}
        }, CLI::ignore_case));

    app.add_option("--session", global_opts.session,
                   "Session id for the current line pointer (default: $SELE_SESSION or 'default')");

    cli::register_init(app, global_opts, exit_code);
    cli::register_new(app, global_opts, exit_code);
    cli::register_append_decision(app, global_opts, exit_code);
    cli::register_status(app, global_opts, exit_code);
    cli::register_lint(app, global_opts, exit_code);
    cli::register_close(app, global_opts, exit_code);
    cli::register_version(app, global_opts, exit_code);
    cli::register_line(app, global_opts, exit_code);
    cli::register_task(app, global_opts, exit_code);
    cli::register_acceptance(app, global_opts, exit_code);
    cli::register_overview(app, global_opts, exit_code);
    cli::register_worktree(app, global_opts, exit_code);

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp& e) {
        std::cout << app.help() << '\n';
        std::exit(exit_codes::SUCCESS);
    } catch (const CLI::CallForAllHelp& e) {
        std::cout << app.help("", CLI::AppFormatMode::All) << '\n';
        std::exit(exit_codes::SUCCESS);
    } catch (const CLI::CallForVersion& e) {
        std::cout << e.what() << '\n';
        std::exit(exit_codes::SUCCESS);
    } catch (const CLI::ParseError& e) {
        const bool json = global_opts.output_format == util::Output::Format::JSON ||
                          wants_json(argc, argv);
        util::Output output(json ? util::Output::Format::JSON : util::Output::Format::TEXT);
        if (json) {
            nlohmann::json err;
            err["error"] = e.what();
            err["suggestion"] = "Run with --help for usage";
            std::cerr << err.dump() << '\n';
        } else {
            std::cerr << "Error: " << e.what() << '\n';
            std::cerr << "Suggestion: Run with --help for usage\n";
        }
        std::exit(exit_codes::USAGE_ERROR);
    } catch (const std::exception& e) {
        const bool json = global_opts.output_format == util::Output::Format::JSON ||
                          wants_json(argc, argv);
        util::Output output(json ? util::Output::Format::JSON : util::Output::Format::TEXT);
        output.error(e.what(), "Run with --help for usage");
        std::exit(exit_codes::ENV_ERROR);
    }

    return exit_code;
}
