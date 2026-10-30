#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/event.hpp"
#include "core/fs.hpp"
#include "core/id.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

void append_event(const core::store::Paths& paths, const std::string& actor,
                  const std::string& op, const nlohmann::json& entity,
                  const nlohmann::json& before, const nlohmann::json& after,
                  const std::string& reason) {
    core::model::Event event;
    event.ts = core::model::now_utc();
    event.actor = actor;
    event.op = op;
    event.entity = entity;
    event.before = before;
    event.after = after;
    event.reason = reason;
    core::event::append(paths, event);
}

nlohmann::json plan_entity(const core::model::Plan& plan) {
    return {{"type", "plan"}, {"plan", plan.id}};
}

struct NewArgs {
    std::string id;
    std::string goal;
    std::vector<std::string> acceptance;
    std::string lines;
    std::string actor{"agent"};
};

struct ListArgs {
    std::string status;
};

struct ShowArgs {
    std::string id;
};

} // namespace

void register_plan_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* plan = app.add_subcommand("plan", "Manage plans");
    add_global_opts(plan, global);

    auto new_args = std::make_shared<NewArgs>();
    auto* new_cmd = plan->add_subcommand("new", "Create a plan");
    add_global_opts(new_cmd, global);
    new_cmd->add_option("id", new_args->id, "Plan id")->required();
    new_cmd->add_option("--goal", new_args->goal, "Plan goal")->required();
    new_cmd->add_option("--acceptance", new_args->acceptance,
                        "Acceptance description (repeatable)");
    new_cmd->add_option("--lines", new_args->lines, "Comma separated line ids");
    new_cmd->add_option("--actor", new_args->actor, "Actor recorded in the event log")
        ->default_val("agent");
    new_cmd->callback([&global, &exit_code, new_args]() {
        const auto ctx = make_context(global);
        if (!core::id::valid_name(new_args->id)) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Invalid plan id: " + new_args->id);
            return;
        }
        if (core::fsys::exists(core::store::plan_path(ctx->paths, new_args->id))) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Plan already exists: " + new_args->id);
            return;
        }

        core::model::Plan result;
        result.id = new_args->id;
        result.goal = new_args->goal;
        result.status = "draft";
        result.created_at = core::model::now_utc();

        int index = 0;
        for (const auto& desc : new_args->acceptance) {
            core::model::Acceptance item;
            item.id = "PA" + std::to_string(++index);
            item.desc = desc;
            result.acceptance.push_back(item);
        }
        for (const auto& line_id : split_csv(new_args->lines)) {
            if (!core::id::valid_name(line_id)) {
                fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Invalid line id: " + line_id);
                return;
            }
            core::model::LineRef ref;
            ref.id = line_id;
            result.lines.push_back(ref);
        }

        try {
            core::store::save_plan(ctx->paths, result);
            append_event(ctx->paths, new_args->actor, "plan.new", plan_entity(result), nullptr,
                         result.status, "create plan");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Created plan " + result.id,
                            {{"plan", result.id}, {"status", result.status}});
    });

    auto list_args = std::make_shared<ListArgs>();
    auto* list_cmd = plan->add_subcommand("list", "List plans");
    add_global_opts(list_cmd, global);
    list_cmd->add_option("--status", list_args->status, "Filter by status");
    list_cmd->callback([&global, &exit_code, list_args]() {
        const auto ctx = make_context(global);
        try {
            nlohmann::json items = nlohmann::json::array();
            for (const auto& item : core::store::load_plans(ctx->paths)) {
                if (!list_args->status.empty() && item.status != list_args->status) continue;
                items.push_back({{"id", item.id},
                                 {"status", item.status},
                                 {"goal", item.goal},
                                 {"lines", item.lines.size()}});
            }
            ctx->out.print_both("Plans: " + std::to_string(items.size()),
                                {{"command", "plan.list"}, {"items", items}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto show_args = std::make_shared<ShowArgs>();
    auto* show_cmd = plan->add_subcommand("show", "Show a plan");
    add_global_opts(show_cmd, global);
    show_cmd->add_option("id", show_args->id, "Plan id")->required();
    show_cmd->callback([&global, &exit_code, show_args]() {
        const auto ctx = make_context(global);
        const auto path = core::store::plan_path(ctx->paths, show_args->id);
        if (!core::fsys::exists(path)) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Plan not found: " + show_args->id);
            return;
        }
        try {
            const auto item = core::model::plan_from_json(
                nlohmann::json::parse(core::fsys::read_file(path)));
            ctx->out.print_both("Plan " + item.id + " [" + item.status + "] " + item.goal,
                                core::model::to_json(item));
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });
}
