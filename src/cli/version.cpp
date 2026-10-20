#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <sstream>
#include <string>

#include "core/version.hpp"
#include "thirdparty/deps.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace cli {

void register_version(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto* cmd_version = app.add_subcommand("version", "Show version information");
    add_output_opt(cmd_version, global_opts);
    cmd_version->footer("Example:\n  sele version\n  sele version --output json");
    cmd_version->callback([&global_opts, &exit_code]() {
        util::Output output(global_opts.output_format);

        nlohmann::json result;
        result["name"] = "sele";
        result["version"] = core::version_string();
        result["cli11"] = deps::cli11_version();
        result["nlohmann_json"] = deps::nlohmann_json_version();

        std::ostringstream text;
        text << "sele " << core::version_string() << '\n'
             << "  cli11: " << deps::cli11_version() << '\n'
             << "  nlohmann_json: " << deps::nlohmann_json_version();

        output.print_both(text.str(), result);
        exit_code = exit_codes::SUCCESS;
    });
}

} // namespace cli
