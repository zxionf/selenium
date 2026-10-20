#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include "core/root.hpp"
#include "core/template.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

void register_append_decision(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        std::string context;
        std::string decision;
        std::string consequences;
        std::string change_id;
    };
    auto args = std::make_shared<Args>();

    auto* cmd_append = app.add_subcommand("append-decision", "Append a decision record to DECISIONS.log");
    add_global_opts(cmd_append, global_opts);
    cmd_append->add_option("--context", args->context, "Context for the decision")->required();
    cmd_append->add_option("--decision", args->decision, "The decision made")->required();
    cmd_append->add_option("--consequences", args->consequences, "Consequences of the decision")->required();
    cmd_append->add_option("--change-id", args->change_id, "Associated change ID (optional)");
    cmd_append->footer("Example:\n  sele append-decision --context \"Use C++20\" --decision \"Adopt modules\" --consequences \"Cleaner imports\" --change-id 20261018-my-feature");
    cmd_append->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        try {
            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);

            fs::path decisions_log = design_root / ".design" / "DECISIONS.log";

            int index = 1;
            if (fs::exists(decisions_log)) {
                std::ifstream in(decisions_log);
                std::string line;
                while (std::getline(in, line)) {
                    if (line.rfind("## [", 0) == 0) {
                        index++;
                    }
                }
            }

            std::ofstream out(decisions_log, std::ios::app);
            if (!fs::exists(decisions_log) || fs::file_size(decisions_log) == 0) {
                out << core::render_decisions_header();
            }
            out << core::render_decision_entry(index,
                args->change_id.empty() ? "none" : args->change_id,
                args->context, args->decision, args->consequences);
            out.close();

            nlohmann::json result;
            result["appended"] = true;
            result["index"] = index;
            result["path"] = decisions_log.string();

            output.print_json(result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
