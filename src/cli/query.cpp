#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/event.hpp"
#include "core/fs.hpp"
#include "core/id.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string context_plan(const core::store::Paths& paths) {
    if (!core::fsys::exists(paths.context)) return "";
    try {
        const auto value = nlohmann::json::parse(core::fsys::read_file(paths.context));
        return value.value("plan", "");
    } catch (...) {
        return "";
    }
}

bool task_ready(const core::model::Task& task, const std::vector<core::model::Plan>& plans,
                const std::vector<core::model::Line>& lines,
                const std::vector<core::model::Task>& tasks) {
    if (task.status != "todo" && task.status != "paused") return false;
    bool plan_ok = false;
    for (const auto& plan : plans) {
        if (plan.id == task.plan && plan.status == "active") plan_ok = true;
    }
    if (!plan_ok) return false;
    bool line_ok = false;
    for (const auto& line : lines) {
        if (line.plan == task.plan && line.id == task.line && line.status == "active") {
            line_ok = true;
        }
    }
    if (!line_ok) return false;
    for (const auto& dep : task.depends) {
        if (dep.value("type", "") != "task") continue;
        const std::string dep_id = dep.value("id", "");
        bool done = false;
        for (const auto& candidate : tasks) {
            if (candidate.plan == task.plan && candidate.line == task.line &&
                candidate.id == dep_id &&
                (candidate.status == "done" || candidate.status == "archived")) {
                done = true;
                break;
            }
        }
        if (!done) return false;
    }
    return true;
}

std::vector<core::model::Task> order_tasks(const std::vector<core::model::Task>& ready) {
    std::map<std::string, core::model::Task> by_key;
    for (const auto& task : ready) by_key[task.plan + "." + task.line + "." + task.id] = task;

    std::vector<core::model::Task> result;
    std::set<std::string> visited;
    std::function<void(const core::model::Task&)> visit = [&](const core::model::Task& task) {
        const auto key = task.plan + "." + task.line + "." + task.id;
        if (!visited.insert(key).second) return;
        for (const auto& dep : task.depends) {
            if (dep.value("type", "") != "task") continue;
            const auto found =
                by_key.find(task.plan + "." + task.line + "." + dep.value("id", ""));
            if (found != by_key.end()) visit(found->second);
        }
        result.push_back(task);
    };
    for (const auto& task : ready) visit(task);
    return result;
}

struct NextArgs {
    std::string plan;
    std::string line;
    bool resume{false};
};

struct StatusArgs {
    std::string plan;
};

struct TreeArgs {
    std::string plan;
    bool all{false};
};

struct LogArgs {
    std::string plan;
    std::string line;
    std::string task;
    bool decisions{false};
    int limit{20};
};

struct CheckArgs {
    std::string plan;
};

} // namespace

void register_query_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto next_args = std::make_shared<NextArgs>();
    auto* next_cmd = app.add_subcommand("next", "List ready tasks in topological order");
    add_global_opts(next_cmd, global);
    next_cmd->add_option("--plan", next_args->plan, "Filter by plan");
    next_cmd->add_option("--line", next_args->line, "Filter by line");
    next_cmd->add_flag("--resume", next_args->resume, "Put paused tasks first");
    next_cmd->callback([&global, &exit_code, next_args]() {
        const auto ctx = make_context(global);
        try {
            const auto plans = core::store::load_plans(ctx->paths);
            const auto lines = core::store::load_lines(ctx->paths);
            const auto tasks = core::store::load_tasks(ctx->paths);

            std::vector<core::model::Task> ready;
            for (const auto& task : tasks) {
                if (!next_args->plan.empty() && task.plan != next_args->plan) continue;
                if (!next_args->line.empty() && task.line != next_args->line) continue;
                if (task_ready(task, plans, lines, tasks)) ready.push_back(task);
            }
            if (next_args->resume) {
                std::stable_sort(ready.begin(), ready.end(),
                                 [](const core::model::Task& left, const core::model::Task& right) {
                                     return left.status == "paused" && right.status != "paused";
                                 });
            }
            ready = order_tasks(ready);

            nlohmann::json items = nlohmann::json::array();
            std::string current;
            for (const auto& task : tasks) {
                if (task.status == "in_progress") current = task.id;
            }
            for (const auto& task : ready) {
                items.push_back({{"id", task.id},
                                 {"uid", task.uid},
                                 {"plan", task.plan},
                                 {"line", task.line},
                                 {"title", task.title},
                                 {"status", task.status}});
            }
            std::ostringstream text;
            text << "Ready tasks: " << ready.size();
            for (const auto& task : ready) {
                text << "\n  " << task.id << " (" << task.plan << "." << task.line << ") "
                     << task.status << "  " << task.title;
            }
            nlohmann::json result = {{"command", "next"}, {"items", items}};
            result["current"] = current.empty() ? nlohmann::json(nullptr) : nlohmann::json(current);
            ctx->out.print_both(text.str(), result);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto status_args = std::make_shared<StatusArgs>();
    auto* status_cmd = app.add_subcommand("status", "Show current state");
    add_global_opts(status_cmd, global);
    status_cmd->add_option("--plan", status_args->plan, "Plan id (defaults to context)");
    status_cmd->callback([&global, &exit_code, status_args]() {
        const auto ctx = make_context(global);
        try {
            std::string plan_id = status_args->plan;
            if (plan_id.empty()) plan_id = context_plan(ctx->paths);

            const auto plans = core::store::load_plans(ctx->paths);
            const auto lines = core::store::load_lines(ctx->paths);
            const auto tasks = core::store::load_tasks(ctx->paths);

            nlohmann::json plan_items = nlohmann::json::array();
            nlohmann::json line_items = nlohmann::json::array();
            nlohmann::json task_counts = nlohmann::json::object();
            std::string current;
            for (const auto& plan : plans) {
                if (!plan_id.empty() && plan.id != plan_id) continue;
                plan_items.push_back({{"id", plan.id}, {"status", plan.status}});
            }
            for (const auto& line : lines) {
                if (!plan_id.empty() && line.plan != plan_id) continue;
                line_items.push_back(
                    {{"plan", line.plan}, {"id", line.id}, {"status", line.status}});
            }
            for (const auto& task : tasks) {
                if (!plan_id.empty() && task.plan != plan_id) continue;
                task_counts[task.status] = task_counts.value(task.status, 0) + 1;
                if (task.status == "in_progress") current = task.id;
            }

            std::ostringstream text;
            text << "Plan: " << (plan_id.empty() ? "<none>" : plan_id);
            text << "\nLines: " << line_items.size();
            for (const auto& line : line_items) {
                text << "\n  " << line["status"].get<std::string>() << "  "
                     << line["plan"].get<std::string>() << "." << line["id"].get<std::string>();
            }
            text << "\nTasks:";
            for (const auto& item : task_counts.items()) {
                text << " " << item.key() << "=" << item.value().get<int>();
            }
            text << "\nCurrent: " << (current.empty() ? "<none>" : current);

            nlohmann::json result = {{"command", "status"},
                                     {"plan", plan_id.empty() ? nlohmann::json(nullptr)
                                                              : nlohmann::json(plan_id)},
                                     {"plans", plan_items},
                                     {"lines", line_items},
                                     {"tasks", task_counts}};
            result["current"] =
                current.empty() ? nlohmann::json(nullptr) : nlohmann::json(current);
            ctx->out.print_both(text.str(), result);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto tree_args = std::make_shared<TreeArgs>();
    auto* tree_cmd = app.add_subcommand("tree", "Show the plan/line/task/step tree");
    add_global_opts(tree_cmd, global);
    tree_cmd->add_option("--plan", tree_args->plan, "Plan id");
    tree_cmd->add_flag("--all", tree_args->all, "Include archived tasks");
    tree_cmd->callback([&global, &exit_code, tree_args]() {
        const auto ctx = make_context(global);
        try {
            const auto plans = core::store::load_plans(ctx->paths);
            const auto lines = core::store::load_lines(ctx->paths);
            const auto tasks = core::store::load_tasks(ctx->paths);

            nlohmann::json plan_items = nlohmann::json::array();
            std::ostringstream text;
            for (const auto& plan : plans) {
                if (!tree_args->plan.empty() && plan.id != tree_args->plan) continue;
                text << plan.id << " [" << plan.status << "] " << plan.goal << "\n";
                nlohmann::json plan_item = {{"id", plan.id}, {"status", plan.status},
                                            {"goal", plan.goal}, {"lines", nlohmann::json::array()}};
                for (const auto& line : lines) {
                    if (line.plan != plan.id) continue;
                    text << "  " << line.id << " [" << line.status << "] " << line.goal << "\n";
                    nlohmann::json line_item = {{"id", line.id}, {"status", line.status},
                                                {"goal", line.goal}, {"tasks", nlohmann::json::array()}};
                    for (const auto& task : tasks) {
                        if (task.plan != plan.id || task.line != line.id) continue;
                        if (!tree_args->all && task.status == "archived") continue;
                        text << "    " << task.id << " [" << task.status << "] " << task.title
                             << "\n";
                        nlohmann::json task_item = {{"id", task.id},
                                                    {"status", task.status},
                                                    {"title", task.title},
                                                    {"steps", nlohmann::json::array()}};
                        for (const auto& step : task.steps) {
                            text << "      " << step.id << " [" << step.status << "] "
                                 << step.title << "\n";
                            task_item["steps"].push_back(
                                {{"id", step.id}, {"status", step.status}, {"title", step.title}});
                        }
                        line_item["tasks"].push_back(task_item);
                    }
                    plan_item["lines"].push_back(line_item);
                }
                plan_items.push_back(plan_item);
            }
            ctx->out.print_both(text.str(), {{"command", "tree"}, {"plans", plan_items}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto log_args = std::make_shared<LogArgs>();
    auto* log_cmd = app.add_subcommand("log", "Show the event log");
    add_global_opts(log_cmd, global);
    log_cmd->add_option("--plan", log_args->plan, "Filter by plan");
    log_cmd->add_option("--line", log_args->line, "Filter by line");
    log_cmd->add_option("--task", log_args->task, "Filter by task id");
    log_cmd->add_flag("--decisions", log_args->decisions, "Only decision events");
    log_cmd->add_option("--limit", log_args->limit, "Maximum events")->default_val(20);
    log_cmd->callback([&global, &exit_code, log_args]() {
        const auto ctx = make_context(global);
        try {
            auto events = core::event::load(ctx->paths);
            std::vector<core::model::Event> filtered;
            for (const auto& event : events) {
                if (log_args->decisions && event.op != "decision") continue;
                if (!log_args->plan.empty() && event.entity.value("plan", "") != log_args->plan) {
                    continue;
                }
                if (!log_args->line.empty() && event.entity.value("line", "") != log_args->line) {
                    continue;
                }
                if (!log_args->task.empty() && event.entity.value("task", "") != log_args->task) {
                    continue;
                }
                filtered.push_back(event);
            }
            const std::size_t limit = log_args->limit < 0 ? 0 : static_cast<std::size_t>(log_args->limit);
            if (filtered.size() > limit) {
                filtered.erase(filtered.begin(), filtered.end() - static_cast<std::ptrdiff_t>(limit));
            }
            nlohmann::json items = nlohmann::json::array();
            std::ostringstream text;
            for (const auto& event : filtered) {
                items.push_back(core::model::to_json(event));
                text << event.ts << "  " << event.op << "  "
                     << event.entity.value("plan", "") << "/"
                     << event.entity.value("line", "") << "/"
                     << event.entity.value("task", "") << "  " << event.reason << "\n";
            }
            ctx->out.print_both(text.str(), {{"command", "log"}, {"items", items}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto check_args = std::make_shared<CheckArgs>();
    auto* check_cmd = app.add_subcommand("check", "Check .sele consistency");
    add_global_opts(check_cmd, global);
    check_cmd->add_option("--plan", check_args->plan, "Plan id");
    check_cmd->callback([&global, &exit_code, check_args]() {
        const auto ctx = make_context(global);
        try {
            std::vector<std::string> problems;
            const auto plans = core::store::load_plans(ctx->paths);
            const auto lines = core::store::load_lines(ctx->paths);
            const auto tasks = core::store::load_tasks(ctx->paths);

            std::map<std::string, const core::model::Line*> line_index;
            std::map<std::string, const core::model::Task*> task_index;
            for (const auto& line : lines) line_index[line.plan + "." + line.id] = &line;
            for (const auto& task : tasks) task_index[task.plan + "." + task.line + "." + task.id] = &task;

            for (const auto& plan : plans) {
                if (!check_args->plan.empty() && plan.id != check_args->plan) continue;
                if (plan.lines.empty()) problems.push_back("plan " + plan.id + " has no line");
                for (const auto& ref : plan.lines) {
                    if (line_index.find(plan.id + "." + ref.id) == line_index.end()) {
                        problems.push_back("plan " + plan.id + " references missing line " + ref.id);
                    }
                }
            }
            for (const auto& line : lines) {
                if (!check_args->plan.empty() && line.plan != check_args->plan) continue;
                if (!core::id::valid_name(line.id)) problems.push_back("invalid line id " + line.id);
            }
            for (const auto& task : tasks) {
                if (!check_args->plan.empty() && task.plan != check_args->plan) continue;
                if (!core::id::valid_name(task.id)) problems.push_back("invalid task id " + task.id);
                if (line_index.find(task.plan + "." + task.line) == line_index.end()) {
                    problems.push_back("task " + task.id + " references missing line");
                }
                for (const auto& dep : task.depends) {
                    if (dep.value("type", "") != "task") continue;
                    const auto key = task.plan + "." + task.line + "." + dep.value("id", "");
                    if (task_index.find(key) == task_index.end()) {
                        problems.push_back("task " + task.id + " depends on missing " +
                                           dep.value("id", ""));
                    }
                }
                if (task.status == "done") {
                    for (const auto& step : task.steps) {
                        if (step.status != "done" && step.status != "skipped") {
                            problems.push_back("done task " + task.id + " has step " + step.id +
                                               " " + step.status);
                        }
                    }
                }
                if (task.uid.empty()) problems.push_back("task " + task.id + " has empty uid");
            }
            try {
                core::event::load(ctx->paths);
            } catch (const std::exception& error) {
                problems.push_back(std::string("event log: ") + error.what());
            }

            nlohmann::json result = {{"command", "check"},
                                     {"ok", problems.empty()},
                                     {"problems", problems}};
            if (!problems.empty()) {
                std::string message = "check failed:";
                for (const auto& problem : problems) message += "\n  " + problem;
                fail(*ctx, exit_code, exit_codes::LINT_FAILED, message);
                return;
            }
            ctx->out.print_both("check passed",
                                {{"command", "check"}, {"ok", true}, {"problems", problems}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });
}
