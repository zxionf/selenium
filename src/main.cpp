#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <iostream>
#include <map>
#include <string>

#include "cli/commands.hpp"
#include "thirdparty/deps.hpp"
#include "util/output.hpp"
#include "util/exit_codes.hpp"

namespace {

std::string version_string() {
    std::string version = "sele 0.1.0 (CLI11 ";
    version += deps::cli11_version();
    version += ", nlohmann/json ";
    version += deps::nlohmann_json_version();
    version += ")";
    return version;
}

} // namespace

int main(int argc, char** argv) {
    CLI::App app{"sele - Manage .design/ and .agent/ directories"};
    app.require_subcommand(1);
    app.set_version_flag("--version", &version_string, "Show version information and exit");

    GlobalOptions global_opts;
    int exit_code = exit_codes::SUCCESS;

    app.add_option("--root", global_opts.root, "Project root directory (default: current dir, search upward for .design/)");
    app.add_option("--output", global_opts.output_format, "Output format: text or json")
        ->transform(CLI::CheckedTransformer(std::map<std::string, util::Output::Format>{
            {"text", util::Output::Format::TEXT},
            {"json", util::Output::Format::JSON}
        }, CLI::ignore_case));

    cli::register_init(app, global_opts, exit_code);
    cli::register_new(app, global_opts, exit_code);
    cli::register_append_decision(app, global_opts, exit_code);
    cli::register_status(app, global_opts, exit_code);
    cli::register_lint(app, global_opts, exit_code);
    cli::register_close(app, global_opts, exit_code);

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
        util::Output output(global_opts.output_format);
        if (global_opts.output_format == util::Output::Format::JSON) {
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
        util::Output output(global_opts.output_format);
        output.error(e.what(), "Run with --help for usage");
        std::exit(exit_codes::ENV_ERROR);
    }

    return exit_code;
}
