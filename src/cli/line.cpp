#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/fs.hpp"
#include "core/id.hpp"
#include "core/state.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

nlohmann::json line_entity(const core::model::Line& line) {
    return {{"type", "line"}, {"plan", line.plan}, {"line", line.id}};
}

bool load_plan(Context& ctx, int& exit_code, const std::string& id, core::model::Plan& out) {
    const auto path = core::store::plan_path(ctx.paths, id);
    if (!core::fsys::exists(path)) {
        fail(ctx, exit_code, exit_codes::USAGE_ERROR, "Plan not found: " + id);
        return false;
    }
    try {
        out = core::model::plan_from_json(nlohmann::json::parse(core::fsys::read_file(path)));
    } catch (const std::exception& error) {
        fail(ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        return false;
    }
    return true;
}

bool load_line(Context& ctx, int& exit_code, const std::string& plan, const std::string& id,
               core::model::Line& out) {
    const auto path = core::store::line_path(ctx.paths, plan, id);
    if (!core::fsys::exists(path)) {
        fail(ctx, exit_code, exit_codes::USAGE_ERROR,
             "Line not found: " + plan + "." + id);
        return false;
    }
    try {
        out = core::model::line_from_json(nlohmann::json::parse(core::fsys::read_file(path)));
    } catch (const std::exception& error) {
        fail(ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        return false;
    }
    return true;
}

bool transition_line(Context& ctx, int& exit_code, core::model::Line& line,
                     const std::string& to) {
    if (!core::state::line_transition(line.status, to)) {
        fail(ctx, exit_code, exit_codes::STATE_CONFLICT,
             "Line " + line.id + " cannot go from " + line.status + " to " + to);
        return false;
    }
    return true;
}

bool reaches(const std::map<std::string, std::vector<std::string>>& graph,
             const std::string& from, const std::string& target, std::set<std::string>& seen) {
    if (from == target) return true;
    if (!seen.insert(from).second) return false;
    const auto found = graph.find(from);
    if (found == graph.end()) return false;
    for (const auto& next : found->second) {
        if (reaches(graph, next, target, seen)) return true;
    }
    return false;
}

struct NewArgs {
    std::string id;
    std::string plan;
    std::string goal;
    std::vector<std::string> acceptance;
    std::vector<std::string> modules;
    std::string actor{"agent"};
};

struct SwitchArgs {
    std::string id;
    std::string plan;
};

struct ActivateArgs {
    std::string id;
    std::string plan;
    std::string reason{"activate line"};
};

struct StatusArgs {
    std::string id;
    std::string plan;
};

struct DoneArgs {
    std::string id;
    std::string plan;
    std::string reason{"line complete"};
};

struct AbandonArgs {
    std::string id;
    std::string plan;
    std::string reason;
};

struct BlockArgs {
    std::string id;
    std::string plan;
    std::string reason;
};

struct ListArgs {
    std::string plan;
    std::string status;
};

struct VerifyArgs {
    std::string id;
    std::string plan;
    std::string acceptance;
    std::string by{"human"};
};

struct DepAddArgs {
    std::string id;
    std::string plan;
    std::string type{"line"};
    std::string target;
};

struct DepRemoveArgs {
    std::string id;
    std::string plan;
    std::string target;
};

struct CheckArgs {
    std::string id;
    std::string plan;
};

} // namespace

void register_line_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* line = app.add_subcommand("line", "Manage lines");
    add_global_opts(line, global);

    auto new_args = std::make_shared<NewArgs>();
    auto* new_cmd = line->add_subcommand("new", "Create a line inside an active plan");
    add_global_opts(new_cmd, global);
    new_cmd->add_option("id", new_args->id, "Line id")->required();
    new_cmd->add_option("--plan", new_args->plan, "Parent plan id")->required();
    new_cmd->add_option("--goal", new_args->goal, "Line goal")->required();
    new_cmd->add_option("--acceptance", new_args->acceptance,
                        "Acceptance description (repeatable)");
    new_cmd->add_option("--modules", new_args->modules, "Allowed modules")
        ->required()
        ->delimiter(',');
    new_cmd->add_option("--actor", new_args->actor, "Actor recorded in the event log")
        ->default_val("agent");
    new_cmd->callback([&global, &exit_code, new_args]() {
        const auto ctx = make_context(global);
        if (!core::id::valid_name(new_args->id)) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Invalid line id: " + new_args->id);
            return;
        }
        core::model::Plan plan;
        if (!load_plan(*ctx, exit_code, new_args->plan, plan)) return;
        if (plan.status != "active") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Plan " + plan.id + " is " + plan.status + ", expected active");
            return;
        }
        if (core::fsys::exists(core::store::line_path(ctx->paths, plan.id, new_args->id))) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Line already exists: " + new_args->id);
            return;
        }

        core::model::Line result;
        result.id = new_args->id;
        result.plan = plan.id;
        result.goal = new_args->goal;
        result.status = "active";
        result.created_at = core::model::now_utc();
        result.modules = new_args->modules;
        int index = 0;
        for (const auto& desc : new_args->acceptance) {
            core::model::Acceptance item;
            item.id = "A" + std::to_string(++index);
            item.desc = desc;
            result.acceptance.push_back(item);
        }

        bool found = false;
        for (const auto& ref : plan.lines) {
            if (ref.id == result.id) found = true;
        }
        if (!found) plan.lines.push_back(core::model::LineRef{result.id});

        try {
            core::store::save_line(ctx->paths, result);
            core::store::save_plan(ctx->paths, plan);
            append_event(ctx->paths, new_args->actor, "line.new", line_entity(result), nullptr,
                         result.status, "create line");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Created line " + result.plan + "." + result.id,
                            {{"plan", plan.id}, {"line", result.id}, {"status", result.status}});
    });

    auto activate_args = std::make_shared<ActivateArgs>();
    auto* activate_cmd = line->add_subcommand("activate", "Activate a draft line");
    add_global_opts(activate_cmd, global);
    activate_cmd->add_option("id", activate_args->id, "Line id")->required();
    activate_cmd->add_option("--plan", activate_args->plan, "Plan id")->required();
    activate_cmd->add_option("--reason", activate_args->reason, "Decision reason");
    activate_cmd->callback([&global, &exit_code, activate_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, activate_args->plan, activate_args->id, item)) return;
        if (!transition_line(*ctx, exit_code, item, "active")) return;
        item.status = "active";
        try {
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, "human", "line.activate", line_entity(item), "draft",
                         item.status, activate_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Activated line " + item.plan + "." + item.id,
                            {{"line", item.id}, {"status", item.status}});
    });

    auto switch_args = std::make_shared<SwitchArgs>();
    auto* switch_cmd = line->add_subcommand("switch", "Set the current line context");
    add_global_opts(switch_cmd, global);
    switch_cmd->add_option("id", switch_args->id, "Line id")->required();
    switch_cmd->add_option("--plan", switch_args->plan, "Plan id (inferred when unique)");
    switch_cmd->callback([&global, &exit_code, switch_args]() {
        const auto ctx = make_context(global);
        std::string plan = switch_args->plan;
        try {
            for (const auto& item : core::store::load_lines(ctx->paths)) {
                if (item.id != switch_args->id) continue;
                if (plan.empty()) plan = item.plan;
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        if (plan.empty()) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Line not found: " + switch_args->id);
            return;
        }
        core::model::Line item;
        if (!load_line(*ctx, exit_code, plan, switch_args->id, item)) return;
        nlohmann::json context = nlohmann::json::object();
        if (core::fsys::exists(ctx->paths.context)) {
            try {
                context = nlohmann::json::parse(core::fsys::read_file(ctx->paths.context));
            } catch (...) {
                context = nlohmann::json::object();
            }
        }
        context["plan"] = plan;
        context["line"] = item.id;
        try {
            core::fsys::write_atomic(ctx->paths.context, context.dump(2) + "\n");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Switched to " + plan + "." + item.id,
                            {{"plan", plan}, {"line", item.id}});
    });

    auto status_args = std::make_shared<StatusArgs>();
    auto* status_cmd = line->add_subcommand("status", "Show a line and its tasks");
    add_global_opts(status_cmd, global);
    status_cmd->add_option("id", status_args->id, "Line id")->required();
    status_cmd->add_option("--plan", status_args->plan, "Plan id")->required();
    status_cmd->callback([&global, &exit_code, status_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, status_args->plan, status_args->id, item)) return;
        nlohmann::json tasks = nlohmann::json::array();
        try {
            for (const auto& task : core::store::load_tasks(ctx->paths)) {
                if (task.plan != item.plan || task.line != item.id) continue;
                tasks.push_back({{"id", task.id},
                                 {"uid", task.uid},
                                 {"status", task.status},
                                 {"steps", task.steps.size()}});
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Line " + item.plan + "." + item.id + " [" + item.status + "]",
                            {{"line", item.id},
                             {"plan", item.plan},
                             {"status", item.status},
                             {"acceptance", item.acceptance.size()},
                             {"tasks", tasks}});
    });

    auto done_args = std::make_shared<DoneArgs>();
    auto* done_cmd = line->add_subcommand("done", "Mark an active line done");
    add_global_opts(done_cmd, global);
    done_cmd->add_option("id", done_args->id, "Line id")->required();
    done_cmd->add_option("--plan", done_args->plan, "Plan id")->required();
    done_cmd->add_option("--reason", done_args->reason, "Decision reason");
    done_cmd->callback([&global, &exit_code, done_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, done_args->plan, done_args->id, item)) return;
        if (!transition_line(*ctx, exit_code, item, "done")) return;
        try {
            for (const auto& task : core::store::load_tasks(ctx->paths)) {
                if (task.plan != item.plan || task.line != item.id) continue;
                if (task.status != "done" && task.status != "archived") {
                    fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                         "Task " + task.id + " is still " + task.status);
                    return;
                }
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        for (const auto& acceptance : item.acceptance) {
            if (acceptance.status != "pass") {
                fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                     "Acceptance " + acceptance.id + " is " + acceptance.status);
                return;
            }
        }
        item.status = "done";
        try {
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, "human", "line.done", line_entity(item), "active",
                         item.status, done_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Closed line " + item.plan + "." + item.id,
                            {{"line", item.id}, {"status", item.status}});
    });

    auto block_args = std::make_shared<BlockArgs>();
    auto* block_cmd = line->add_subcommand("block", "Block a line");
    add_global_opts(block_cmd, global);
    block_cmd->add_option("id", block_args->id, "Line id")->required();
    block_cmd->add_option("--plan", block_args->plan, "Plan id")->required();
    block_cmd->add_option("--reason", block_args->reason, "Block reason")->required();
    block_cmd->callback([&global, &exit_code, block_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, block_args->plan, block_args->id, item)) return;
        if (!transition_line(*ctx, exit_code, item, "blocked")) return;
        item.status = "blocked";
        try {
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, "human", "line.block", line_entity(item), "active",
                         item.status, block_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Blocked line " + item.plan + "." + item.id,
                            {{"line", item.id}, {"status", item.status}});
    });

    auto unblock_args = std::make_shared<SwitchArgs>();
    auto* unblock_cmd = line->add_subcommand("unblock", "Unblock a line");
    add_global_opts(unblock_cmd, global);
    unblock_cmd->add_option("id", unblock_args->id, "Line id")->required();
    unblock_cmd->add_option("--plan", unblock_args->plan, "Plan id")->required();
    unblock_cmd->callback([&global, &exit_code, unblock_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, unblock_args->plan, unblock_args->id, item)) return;
        if (!transition_line(*ctx, exit_code, item, "active")) return;
        item.status = "active";
        try {
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, "human", "line.unblock", line_entity(item), "blocked",
                         item.status, "unblock");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Unblocked line " + item.plan + "." + item.id,
                            {{"line", item.id}, {"status", item.status}});
    });

    auto abandon_args = std::make_shared<AbandonArgs>();
    auto* abandon_cmd = line->add_subcommand("abandon", "Abandon a line");
    add_global_opts(abandon_cmd, global);
    abandon_cmd->add_option("id", abandon_args->id, "Line id")->required();
    abandon_cmd->add_option("--plan", abandon_args->plan, "Plan id")->required();
    abandon_cmd->add_option("--reason", abandon_args->reason, "Abandon reason")->required();
    abandon_cmd->callback([&global, &exit_code, abandon_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, abandon_args->plan, abandon_args->id, item)) return;
        if (!transition_line(*ctx, exit_code, item, "abandoned")) return;
        item.status = "abandoned";
        try {
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, "human", "line.abandon", line_entity(item), "active",
                         item.status, abandon_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Abandoned line " + item.plan + "." + item.id,
                            {{"line", item.id}, {"status", item.status}});
    });

    auto list_args = std::make_shared<ListArgs>();
    auto* list_cmd = line->add_subcommand("list", "List lines");
    add_global_opts(list_cmd, global);
    list_cmd->add_option("--plan", list_args->plan, "Filter by plan");
    list_cmd->add_option("--status", list_args->status, "Filter by status");
    list_cmd->callback([&global, &exit_code, list_args]() {
        const auto ctx = make_context(global);
        try {
            nlohmann::json items = nlohmann::json::array();
            for (const auto& item : core::store::load_lines(ctx->paths)) {
                if (!list_args->plan.empty() && item.plan != list_args->plan) continue;
                if (!list_args->status.empty() && item.status != list_args->status) continue;
                items.push_back({{"plan", item.plan},
                                 {"id", item.id},
                                 {"status", item.status},
                                 {"goal", item.goal}});
            }
            std::ostringstream text;
            text << "Lines: " << items.size();
            for (const auto& item : items) {
                text << "\n  " << item["status"].get<std::string>() << "  "
                     << item["plan"].get<std::string>() << "."
                     << item["id"].get<std::string>() << "  "
                     << item["goal"].get<std::string>();
            }
            ctx->out.print_both(text.str(), {{"command", "line.list"}, {"items", items}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto verify_args = std::make_shared<VerifyArgs>();
    auto* verify_cmd = line->add_subcommand("verify", "Verify a line acceptance point");
    add_global_opts(verify_cmd, global);
    verify_cmd->add_option("id", verify_args->id, "Line id")->required();
    verify_cmd->add_option("--plan", verify_args->plan, "Plan id")->required();
    verify_cmd->add_option("acceptance", verify_args->acceptance, "Acceptance id")->required();
    verify_cmd->add_option("--by", verify_args->by, "Verifier identity")->default_val("human");
    verify_cmd->callback([&global, &exit_code, verify_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, verify_args->plan, verify_args->id, item)) return;
        bool found = false;
        for (auto& acceptance : item.acceptance) {
            if (acceptance.id != verify_args->acceptance) continue;
            acceptance.status = "pass";
            acceptance.verified_by = verify_args->by;
            acceptance.verified_at = core::model::now_utc();
            found = true;
            break;
        }
        if (!found) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Acceptance not found: " + verify_args->acceptance);
            return;
        }
        try {
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, verify_args->by, "line.verify", line_entity(item),
                         "unverified", "pass", verify_args->acceptance);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Verified " + verify_args->acceptance + " on line " + item.id,
                            {{"line", item.id},
                             {"acceptance", verify_args->acceptance},
                             {"status", "pass"}});
    });

    auto dep_add_args = std::make_shared<DepAddArgs>();
    auto* dep_add_cmd = line->add_subcommand("dep-add", "Add a line dependency");
    add_global_opts(dep_add_cmd, global);
    dep_add_cmd->add_option("id", dep_add_args->id, "Line id")->required();
    dep_add_cmd->add_option("--plan", dep_add_args->plan, "Plan id")->required();
    dep_add_cmd->add_option("--type", dep_add_args->type, "Dependency type: line|task")
        ->default_val("line")
        ->check(CLI::IsMember({"line", "task"}));
    dep_add_cmd->add_option("--target", dep_add_args->target, "Target line id")->required();
    dep_add_cmd->callback([&global, &exit_code, dep_add_args]() {
        const auto ctx = make_context(global);
        if (dep_add_args->type != "line") {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Task dependencies are not implemented yet");
            return;
        }
        core::model::Line item;
        if (!load_line(*ctx, exit_code, dep_add_args->plan, dep_add_args->id, item)) return;
        try {
            std::vector<core::model::Line> lines = core::store::load_lines(ctx->paths);
            bool target_exists = false;
            for (const auto& candidate : lines) {
                if (candidate.plan == item.plan && candidate.id == dep_add_args->target) {
                    target_exists = true;
                }
            }
            if (!target_exists) {
                fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                     "Target line not found: " + dep_add_args->target);
                return;
            }

            std::map<std::string, std::vector<std::string>> graph;
            for (const auto& candidate : lines) {
                auto& edges = graph[candidate.id];
                for (const auto& dep : candidate.depends) {
                    if (dep.value("type", "") == "line") {
                        edges.push_back(dep.value("id", ""));
                    }
                }
            }
            auto& edges = graph[item.id];
            edges.push_back(dep_add_args->target);
            std::set<std::string> seen;
            if (reaches(graph, dep_add_args->target, item.id, seen)) {
                fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                     "Dependency would create a cycle through " + dep_add_args->target);
                return;
            }

            item.depends.push_back({{"type", "line"}, {"id", dep_add_args->target}});
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, "human", "line.dep", line_entity(item), nullptr,
                         item.depends, dep_add_args->target);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Added dependency " + dep_add_args->target + " to " + item.id,
                            {{"line", item.id}, {"depends", item.depends}});
    });

    auto dep_remove_args = std::make_shared<DepRemoveArgs>();
    auto* dep_remove_cmd = line->add_subcommand("dep-remove", "Remove a line dependency");
    add_global_opts(dep_remove_cmd, global);
    dep_remove_cmd->add_option("id", dep_remove_args->id, "Line id")->required();
    dep_remove_cmd->add_option("--plan", dep_remove_args->plan, "Plan id")->required();
    dep_remove_cmd->add_option("--target", dep_remove_args->target, "Target line id")->required();
    dep_remove_cmd->callback([&global, &exit_code, dep_remove_args]() {
        const auto ctx = make_context(global);
        core::model::Line item;
        if (!load_line(*ctx, exit_code, dep_remove_args->plan, dep_remove_args->id, item)) return;
        nlohmann::json kept = nlohmann::json::array();
        bool removed = false;
        for (const auto& dep : item.depends) {
            if (dep.value("type", "") == "line" && dep.value("id", "") == dep_remove_args->target) {
                removed = true;
                continue;
            }
            kept.push_back(dep);
        }
        if (!removed) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Dependency not found: " + dep_remove_args->target);
            return;
        }
        item.depends = kept;
        try {
            core::store::save_line(ctx->paths, item);
            append_event(ctx->paths, "human", "line.dep", line_entity(item), nullptr,
                         item.depends, "remove " + dep_remove_args->target);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Removed dependency " + dep_remove_args->target + " from " + item.id,
                            {{"line", item.id}, {"depends", item.depends}});
    });

    auto check_args = std::make_shared<CheckArgs>();
    auto* check_cmd = line->add_subcommand("check", "Check line dependency cycles");
    add_global_opts(check_cmd, global);
    check_cmd->add_option("id", check_args->id, "Line id");
    check_cmd->add_option("--plan", check_args->plan, "Plan id");
    check_cmd->callback([&global, &exit_code, check_args]() {
        const auto ctx = make_context(global);
        try {
            std::map<std::string, std::vector<std::string>> graph;
            for (const auto& item : core::store::load_lines(ctx->paths)) {
                if (!check_args->plan.empty() && item.plan != check_args->plan) continue;
                if (!check_args->id.empty() && item.id != check_args->id) continue;
                auto& edges = graph[item.id];
                for (const auto& dep : item.depends) {
                    if (dep.value("type", "") == "line") edges.push_back(dep.value("id", ""));
                }
            }
            std::set<std::string> bad;
            for (const auto& node : graph) {
                for (const auto& next : node.second) {
                    std::set<std::string> seen;
                    if (reaches(graph, next, node.first, seen)) {
                        bad.insert(node.first);
                        break;
                    }
                }
            }
            if (!bad.empty()) {
                std::string names;
                for (const auto& name : bad) names += (names.empty() ? "" : ",") + name;
                fail(*ctx, exit_code, exit_codes::LINT_FAILED, "Dependency cycle: " + names);
                return;
            }
            ctx->out.print_both("Line check passed",
                                {{"command", "line.check"}, {"ok", true},
                                 {"lines", graph.size()}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });
}
