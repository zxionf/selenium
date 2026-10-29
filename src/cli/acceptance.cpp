#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>

#include "cli/nextgen.hpp"
#include "core/model.hpp"
#include "core/sele.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace cli {

namespace {

using ng::fail;

struct AcceptanceArgs {
    std::string id;
    std::string line;
    std::string evidence;
    std::string by;
};

void record_acceptance(CLI::App& app, GlobalOptions& global_opts, int& exit_code,
                       const std::string& name, const std::string& status) {
    auto args = std::make_shared<AcceptanceArgs>();
    auto* cmd = app.add_subcommand(name, status == "pass" ? "Record an acceptance item as passing"
                                                          : "Record an acceptance item as failing");
    add_global_opts(cmd, global_opts);
    cmd->add_option("acceptance", args->id, "Acceptance id (A1, A2, ...)")->required();
    cmd->add_option("--line", args->line, "Line id (default: session pointer)");
    cmd->add_option("--evidence", args->evidence, "Evidence text (command output, reviewer note)")
        ->required();
    cmd->add_option("--by", args->by, "Who verified it (default: actor)");
    cmd->footer(std::string("Example:\n  sele acceptance ") + name +
                " A1 --evidence \"pytest 12 passed\"");

    cmd->callback([&global_opts, &exit_code, args, status]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, args->line);
            ng::require_active(ctx, "acceptance " + status);

            bool found = false;
            for (const auto& item : ctx.line.acceptance) {
                if (item.id == args->id) found = true;
            }
            if (!found) {
                fail(exit_codes::STATE_CONFLICT,
                     "Acceptance item not found: " + args->id,
                     "List acceptance ids with sele line show",
                     {"sele line show " + ctx.line_id});
            }

            auto& state = ctx.acc_state(args->id);
            if (state.status == status) {
                nlohmann::json result;
                result["command"] = "acceptance." + status;
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["acceptance"] = args->id;
                result["status"] = status;
                output.print_both("Acceptance " + args->id + " is already " + status +
                                      " (unchanged)",
                                  result);
                return;
            }

            const std::string by = args->by.empty() ? ctx.actor : args->by;
            const std::string now = core::sele::utc_now();
            state.status = status;
            state.evidence = args->evidence;
            state.by = by;
            state.at = now;
            ng::save_runtime(ctx);
            ng::append_log(ctx, "acceptance." + status,
                           {{"acceptance", args->id}, {"evidence", args->evidence}});
            ng::append_event(ctx, "acceptance." + status,
                             {{"acceptance", args->id}, {"by", by}});

            nlohmann::json result;
            result["command"] = "acceptance." + status;
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["acceptance"] = args->id;
            result["status"] = status;
            result["evidence"] = args->evidence;
            result["by"] = by;
            output.print_both("Acceptance " + args->id + " -> " + status + " (" + args->evidence +
                                  ")",
                              result);
        });
    });
}

} // namespace

void register_acceptance(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto* group = app.add_subcommand("acceptance", "Record acceptance results (no command runs)");
    group->require_subcommand(1);
    record_acceptance(*group, global_opts, exit_code, "pass", "pass");
    record_acceptance(*group, global_opts, exit_code, "fail", "fail");
}

} // namespace cli
