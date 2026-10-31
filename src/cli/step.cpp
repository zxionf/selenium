#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/fs.hpp"
#include "core/state.hpp"
#include "core/store.hpp"
#include "util/color.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

nlohmann::json step_entity(const core::model::Task& task, const core::model::Step& step) {
    return {{"type", "step"},
            {"plan", task.plan},
            {"line", task.line},
            {"task", task.id},
            {"step", step.id}};
}

bool load_task(Context& ctx, int& exit_code, const std::string& id, const std::string& plan,
               const std::string& line, core::model::Task& out) {
    std::vector<core::model::Task> matches;
    const auto ref = parse_task_ref(id, plan, line);
    try {
        for (const auto& task : core::store::load_tasks(ctx.paths)) {
            if (task.id != ref.id) continue;
            if (!ref.plan.empty() && task.plan != ref.plan) continue;
            if (!ref.line.empty() && task.line != ref.line) continue;
            matches.push_back(task);
        }
    } catch (const std::exception& error) {
        fail(ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        return false;
    }
    if (matches.empty()) {
        fail(ctx, exit_code, exit_codes::USAGE_ERROR, "Task not found: " + id);
        return false;
    }
    if (matches.size() > 1) {
        fail(ctx, exit_code, exit_codes::USAGE_ERROR,
             "Task id is ambiguous across lines; pass --plan and --line");
        return false;
    }
    out = matches.front();
    return true;
}

core::model::Step* find_step(core::model::Task& task, const std::string& id) {
    for (auto& step : task.steps) {
        if (step.id == id) return &step;
    }
    return nullptr;
}

std::string next_step_id(const core::model::Task& task) {
    int highest = 0;
    for (const auto& step : task.steps) {
        if (step.id.size() < 2 || step.id[0] != 'S') continue;
        try {
            highest = std::max(highest, std::stoi(step.id.substr(1)));
        } catch (...) {
        }
    }
    return "S" + std::to_string(highest + 1);
}

bool transition_step(Context& ctx, int& exit_code, const core::model::Step& step,
                     const std::string& to) {
    if (!core::state::step_transition(step.status, to)) {
        fail(ctx, exit_code, exit_codes::STATE_CONFLICT,
             "Step " + step.id + " cannot go from " + step.status + " to " + to);
        return false;
    }
    return true;
}

bool save_task(Context& ctx, int& exit_code, const core::model::Task& task) {
    try {
        core::store::save_task(ctx.paths, task);
    } catch (const std::exception& error) {
        fail(ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        return false;
    }
    return true;
}

struct AddArgs {
    std::string task;
    std::string title;
    std::string after;
    std::string plan;
    std::string line;
    bool requires_review{false};
};

struct StepArgs {
    std::string task;
    std::string step;
    std::string plan;
    std::string line;
};

struct DoneArgs {
    std::string task;
    std::string step;
    std::string plan;
    std::string line;
};

struct SkipArgs {
    std::string task;
    std::string step;
    std::string plan;
    std::string line;
    std::string reason;
};

struct ListArgs {
    std::string task;
    std::string plan;
    std::string line;
};

struct UpdateArgs {
    std::string task;
    std::string step;
    std::string plan;
    std::string line;
    std::string title;
    std::string desc;
    bool review{false};
    bool no_review{false};
};

struct MoveStepArgs {
    std::string task;
    std::string step;
    std::string plan;
    std::string line;
    std::string after;
};

struct GotoArgs {
    std::string task;
    std::string step;
    std::string plan;
    std::string line;
    std::string reason;
};

} // namespace

void register_step_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* step = app.add_subcommand("step", "Manage steps inside a task");
    add_global_opts(step, global);

    struct StepViewArgs {
        bool all{false};
        bool global{false};
    };
    auto view_args = std::make_shared<StepViewArgs>();
    step->add_flag("--all", view_args->all, "Show steps of all tasks in the current line");
    step->add_flag("--global", view_args->global, "Show steps of all plans");
    step->callback([&global, &exit_code, view_args, step]() {
        for (const auto* sub : step->get_subcommands()) {
            if (sub->count() > 0) return;
        }
        const auto ctx = make_context(global);
        std::string plan_id;
        std::string line_id;
        if (core::fsys::exists(ctx->paths.context)) {
            try {
                const auto value = nlohmann::json::parse(core::fsys::read_file(ctx->paths.context));
                plan_id = value.value("plan", "");
                line_id = value.value("line", "");
            } catch (...) {
            }
        }

        std::vector<core::model::Task> selected;
        try {
            for (const auto& task : core::store::load_tasks(ctx->paths)) {
                if (view_args->global) {
                    selected.push_back(task);
                    continue;
                }
                if (view_args->all) {
                    if (!line_id.empty() && task.line != line_id) continue;
                    if (!plan_id.empty() && task.plan != plan_id) continue;
                    selected.push_back(task);
                    continue;
                }
                if (task.status == "in_progress") selected.push_back(task);
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        if (selected.empty() && !view_args->all && !view_args->global) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "No active task; run sele task start <id> or sele step --all");
            return;
        }
        if (view_args->all || view_args->global) selected = topo_sort(selected);

        std::ostringstream text;
        nlohmann::json tasks_json = nlohmann::json::array();
        for (const auto& task : selected) {
            text << "Task " << task.id << " " << task.title << " [" << task.status << "]\n";
            nlohmann::json items = nlohmann::json::array();
            for (const auto& item : task.steps) {
                text << "  " << item.id << "  " << item.title << "  "
                     << util::color::status(item.status) << "\n";
                items.push_back({{"id", item.id},
                                 {"title", item.title},
                                 {"status", item.status},
                                 {"requires_review", item.requires_review},
                                 {"locked", item.locked}});
            }
            tasks_json.push_back({{"id", task.id},
                                  {"title", task.title},
                                  {"status", task.status},
                                  {"current_step", task.current_step.empty()
                                                       ? nlohmann::json(nullptr)
                                                       : nlohmann::json(task.current_step)},
                                  {"steps", items}});
        }
        ctx->out.print_both(text.str(), {{"command", "step"}, {"tasks", tasks_json}});
    });

    auto add_args = std::make_shared<AddArgs>();
    auto* add_cmd = step->add_subcommand("add", "Add a step to a task");
    add_global_opts(add_cmd, global);
    add_cmd->add_option("task", add_args->task, "Task id")->required();
    add_cmd->add_option("title", add_args->title, "Step title")->required();
    add_cmd->add_option("--after", add_args->after, "Insert after this step id");
    add_cmd->add_option("--plan", add_args->plan, "Plan id");
    add_cmd->add_option("--line", add_args->line, "Line id");
    add_cmd->add_flag("--requires-review", add_args->requires_review,
                      "Requires human review");
    add_cmd->callback([&global, &exit_code, add_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, add_args->task, add_args->plan, add_args->line, task)) {
            return;
        }
        core::model::Step item;
        item.id = next_step_id(task);
        item.title = add_args->title;
        item.status = "todo";
        item.requires_review = add_args->requires_review;

        if (!add_args->after.empty()) {
            auto position = task.steps.begin();
            bool found = false;
            for (; position != task.steps.end(); ++position) {
                if (position->id == add_args->after) {
                    found = true;
                    ++position;
                    break;
                }
            }
            if (!found) {
                fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                     "Step not found: " + add_args->after);
                return;
            }
            task.steps.insert(position, item);
        } else {
            task.steps.push_back(item);
        }

        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.add", step_entity(task, item), nullptr,
                         item.status, "create step");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Added " + item.id + " to " + task.id,
                            {{"task", task.id}, {"step", item.id}, {"status", item.status}});
    });

    auto start_args = std::make_shared<StepArgs>();
    auto* start_cmd = step->add_subcommand("start", "Start a step");
    add_global_opts(start_cmd, global);
    start_cmd->add_option("task", start_args->task, "Task id")->required();
    start_cmd->add_option("step", start_args->step, "Step id")->required();
    start_cmd->add_option("--plan", start_args->plan, "Plan id");
    start_cmd->add_option("--line", start_args->line, "Line id");
    start_cmd->callback([&global, &exit_code, start_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, start_args->task, start_args->plan, start_args->line,
                       task)) {
            return;
        }
        if (task.status != "in_progress") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Task " + task.id + " is " + task.status + ", expected in_progress");
            return;
        }
        auto* item = find_step(task, start_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Step not found: " + start_args->step);
            return;
        }
        if (item->locked) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + item->id + " is locked: " + item->lock_reason);
            return;
        }
        if (!transition_step(*ctx, exit_code, *item, "in_progress")) return;
        for (const auto& other : task.steps) {
            if (other.id != item->id && other.status == "in_progress") {
                fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                     "Step " + other.id + " is already in_progress (I2)");
                return;
            }
        }
        item->status = "in_progress";
        task.current_step = item->id;
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.start", step_entity(task, *item), "todo",
                         item->status, "start step");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Started " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto done_args = std::make_shared<DoneArgs>();
    auto* done_cmd = step->add_subcommand("done", "Finish a step");
    add_global_opts(done_cmd, global);
    done_cmd->add_option("task", done_args->task, "Task id")->required();
    done_cmd->add_option("step", done_args->step, "Step id")->required();
    done_cmd->add_option("--plan", done_args->plan, "Plan id");
    done_cmd->add_option("--line", done_args->line, "Line id");
    done_cmd->callback([&global, &exit_code, done_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, done_args->task, done_args->plan, done_args->line, task)) {
            return;
        }
        auto* item = find_step(task, done_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Step not found: " + done_args->step);
            return;
        }
        if (item->locked) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + item->id + " is locked: " + item->lock_reason);
            return;
        }
        const std::string target = item->requires_review ? "pending_review" : "done";
        if (!transition_step(*ctx, exit_code, *item, target)) return;
        item->status = target;
        if (task.current_step == item->id) task.current_step.clear();
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.done", step_entity(task, *item),
                         "in_progress", item->status, "finish step");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Finished " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto skip_args = std::make_shared<SkipArgs>();
    auto* skip_cmd = step->add_subcommand("skip", "Skip a step");
    add_global_opts(skip_cmd, global);
    skip_cmd->add_option("task", skip_args->task, "Task id")->required();
    skip_cmd->add_option("step", skip_args->step, "Step id")->required();
    skip_cmd->add_option("--plan", skip_args->plan, "Plan id");
    skip_cmd->add_option("--line", skip_args->line, "Line id");
    skip_cmd->add_option("--reason", skip_args->reason, "Skip reason")->required();
    skip_cmd->callback([&global, &exit_code, skip_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, skip_args->task, skip_args->plan, skip_args->line, task)) {
            return;
        }
        auto* item = find_step(task, skip_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Step not found: " + skip_args->step);
            return;
        }
        if (item->locked) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + item->id + " is locked: " + item->lock_reason);
            return;
        }
        if (!transition_step(*ctx, exit_code, *item, "skipped")) return;
        item->status = "skipped";
        item->skipped_reason = skip_args->reason;
        if (task.current_step == item->id) task.current_step.clear();
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.skip", step_entity(task, *item), "todo",
                         item->status, skip_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Skipped " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto goto_args = std::make_shared<GotoArgs>();
    auto* goto_cmd = step->add_subcommand("goto", "Jump to a step; skipped steps are recorded");
    add_global_opts(goto_cmd, global);
    goto_cmd->add_option("task", goto_args->task, "Task id")->required();
    goto_cmd->add_option("step", goto_args->step, "Target step id")->required();
    goto_cmd->add_option("--plan", goto_args->plan, "Plan id");
    goto_cmd->add_option("--line", goto_args->line, "Line id");
    goto_cmd->add_option("--reason", goto_args->reason, "Jump reason")->required();
    goto_cmd->callback([&global, &exit_code, goto_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, goto_args->task, goto_args->plan, goto_args->line, task)) {
            return;
        }
        if (task.status != "in_progress") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Task " + task.id + " is " + task.status + ", expected in_progress");
            return;
        }
        auto* target = find_step(task, goto_args->step);
        if (target == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Step not found: " + goto_args->step);
            return;
        }
        if (target->status != "todo") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Target step " + target->id + " is " + target->status + ", expected todo");
            return;
        }
        for (auto& item : task.steps) {
            if (item.id == target->id) continue;
            if (item.status == "todo" || item.status == "in_progress") {
                item.status = "skipped";
                item.skipped_reason = "jumped to " + target->id;
                item.jump_meta = goto_args->reason;
                append_event(ctx->paths, "agent", "step.skip", step_entity(task, item),
                             "todo", item.status, "jump to " + target->id);
            }
        }
        target->status = "in_progress";
        task.current_step = target->id;
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.goto", step_entity(task, *target), nullptr,
                         target->status, goto_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Jumped to " + target->id + " in " + task.id,
                            {{"task", task.id}, {"step", target->id}, {"status", target->status}});
    });

    auto lock_args = std::make_shared<SkipArgs>();
    auto* lock_cmd = step->add_subcommand("lock", "Lock a step");
    add_global_opts(lock_cmd, global);
    lock_cmd->add_option("task", lock_args->task, "Task id")->required();
    lock_cmd->add_option("step", lock_args->step, "Step id")->required();
    lock_cmd->add_option("--plan", lock_args->plan, "Plan id");
    lock_cmd->add_option("--line", lock_args->line, "Line id");
    lock_cmd->add_option("--reason", lock_args->reason, "Lock reason")->required();
    lock_cmd->callback([&global, &exit_code, lock_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, lock_args->task, lock_args->plan, lock_args->line,
                       task)) {
            return;
        }
        auto* item = find_step(task, lock_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Step not found: " + lock_args->step);
            return;
        }
        item->locked = true;
        item->lock_reason = lock_args->reason;
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "human", "step.lock", step_entity(task, *item), nullptr,
                         "locked", lock_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Locked " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"locked", true}});
    });

    auto unlock_args = std::make_shared<StepArgs>();
    auto* unlock_cmd = step->add_subcommand("unlock", "Unlock a step");
    add_global_opts(unlock_cmd, global);
    unlock_cmd->add_option("task", unlock_args->task, "Task id")->required();
    unlock_cmd->add_option("step", unlock_args->step, "Step id")->required();
    unlock_cmd->add_option("--plan", unlock_args->plan, "Plan id");
    unlock_cmd->add_option("--line", unlock_args->line, "Line id");
    unlock_cmd->callback([&global, &exit_code, unlock_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, unlock_args->task, unlock_args->plan, unlock_args->line,
                       task)) {
            return;
        }
        auto* item = find_step(task, unlock_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + unlock_args->step);
            return;
        }
        item->locked = false;
        item->lock_reason.clear();
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "human", "step.unlock", step_entity(task, *item),
                         "locked", "todo", "unlock step");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Unlocked " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"locked", false}});
    });

    auto fail_step_args = std::make_shared<SkipArgs>();
    auto* fail_step_cmd = step->add_subcommand("fail", "Mark a step failed");
    add_global_opts(fail_step_cmd, global);
    fail_step_cmd->add_option("task", fail_step_args->task, "Task id")->required();
    fail_step_cmd->add_option("step", fail_step_args->step, "Step id")->required();
    fail_step_cmd->add_option("--plan", fail_step_args->plan, "Plan id");
    fail_step_cmd->add_option("--line", fail_step_args->line, "Line id");
    fail_step_cmd->add_option("--reason", fail_step_args->reason, "Failure reason")->required();
    fail_step_cmd->callback([&global, &exit_code, fail_step_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, fail_step_args->task, fail_step_args->plan,
                       fail_step_args->line, task)) {
            return;
        }
        auto* item = find_step(task, fail_step_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + fail_step_args->step);
            return;
        }
        if (item->locked) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + item->id + " is locked: " + item->lock_reason);
            return;
        }
        if (!transition_step(*ctx, exit_code, *item, "failed")) return;
        item->status = "failed";
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.fail", step_entity(task, *item),
                         "in_progress", item->status, fail_step_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Failed " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto block_step_args = std::make_shared<SkipArgs>();
    auto* block_step_cmd = step->add_subcommand("block", "Block a step");
    add_global_opts(block_step_cmd, global);
    block_step_cmd->add_option("task", block_step_args->task, "Task id")->required();
    block_step_cmd->add_option("step", block_step_args->step, "Step id")->required();
    block_step_cmd->add_option("--plan", block_step_args->plan, "Plan id");
    block_step_cmd->add_option("--line", block_step_args->line, "Line id");
    block_step_cmd->add_option("--reason", block_step_args->reason, "Block reason")->required();
    block_step_cmd->callback([&global, &exit_code, block_step_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, block_step_args->task, block_step_args->plan,
                       block_step_args->line, task)) {
            return;
        }
        auto* item = find_step(task, block_step_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + block_step_args->step);
            return;
        }
        if (item->locked) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + item->id + " is locked: " + item->lock_reason);
            return;
        }
        if (!transition_step(*ctx, exit_code, *item, "blocked")) return;
        item->status = "blocked";
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.block", step_entity(task, *item),
                         "in_progress", item->status, block_step_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Blocked " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto unblock_step_args = std::make_shared<StepArgs>();
    auto* unblock_step_cmd = step->add_subcommand("unblock", "Unblock a step");
    add_global_opts(unblock_step_cmd, global);
    unblock_step_cmd->add_option("task", unblock_step_args->task, "Task id")->required();
    unblock_step_cmd->add_option("step", unblock_step_args->step, "Step id")->required();
    unblock_step_cmd->add_option("--plan", unblock_step_args->plan, "Plan id");
    unblock_step_cmd->add_option("--line", unblock_step_args->line, "Line id");
    unblock_step_cmd->callback([&global, &exit_code, unblock_step_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, unblock_step_args->task, unblock_step_args->plan,
                       unblock_step_args->line, task)) {
            return;
        }
        auto* item = find_step(task, unblock_step_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + unblock_step_args->step);
            return;
        }
        if (!transition_step(*ctx, exit_code, *item, "in_progress")) return;
        item->status = "in_progress";
        task.current_step = item->id;
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "human", "step.unblock", step_entity(task, *item),
                         "blocked", item->status, "unblock step");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Unblocked " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto approve_args = std::make_shared<StepArgs>();
    auto* approve_cmd = step->add_subcommand("approve", "Approve a pending step");
    add_global_opts(approve_cmd, global);
    approve_cmd->add_option("task", approve_args->task, "Task id")->required();
    approve_cmd->add_option("step", approve_args->step, "Step id")->required();
    approve_cmd->add_option("--plan", approve_args->plan, "Plan id");
    approve_cmd->add_option("--line", approve_args->line, "Line id");
    approve_cmd->callback([&global, &exit_code, approve_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, approve_args->task, approve_args->plan, approve_args->line,
                       task)) {
            return;
        }
        auto* item = find_step(task, approve_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + approve_args->step);
            return;
        }
        if (!transition_step(*ctx, exit_code, *item, "done")) return;
        item->status = "done";
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "human", "step.approve", step_entity(task, *item),
                         "pending_review", item->status, "approved");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Approved " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto reject_args = std::make_shared<SkipArgs>();
    auto* reject_cmd = step->add_subcommand("reject", "Reject a pending step");
    add_global_opts(reject_cmd, global);
    reject_cmd->add_option("task", reject_args->task, "Task id")->required();
    reject_cmd->add_option("step", reject_args->step, "Step id")->required();
    reject_cmd->add_option("--plan", reject_args->plan, "Plan id");
    reject_cmd->add_option("--line", reject_args->line, "Line id");
    reject_cmd->add_option("--reason", reject_args->reason, "Reject reason")->required();
    reject_cmd->callback([&global, &exit_code, reject_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, reject_args->task, reject_args->plan, reject_args->line,
                       task)) {
            return;
        }
        auto* item = find_step(task, reject_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + reject_args->step);
            return;
        }
        if (!transition_step(*ctx, exit_code, *item, "in_progress")) return;
        item->status = "in_progress";
        task.current_step = item->id;
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "human", "step.reject", step_entity(task, *item),
                         "pending_review", item->status, reject_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Rejected " + item->id + " in " + task.id,
                            {{"task", task.id}, {"step", item->id}, {"status", item->status}});
    });

    auto update_args = std::make_shared<UpdateArgs>();
    auto* update_cmd = step->add_subcommand("update", "Update step metadata");
    add_global_opts(update_cmd, global);
    update_cmd->add_option("task", update_args->task, "Task id")->required();
    update_cmd->add_option("step", update_args->step, "Step id")->required();
    update_cmd->add_option("--plan", update_args->plan, "Plan id");
    update_cmd->add_option("--line", update_args->line, "Line id");
    update_cmd->add_option("--title", update_args->title, "New title");
    update_cmd->add_option("--desc", update_args->desc, "New description");
    update_cmd->add_flag("--requires-review", update_args->review, "Require review");
    update_cmd->add_flag("--no-requires-review", update_args->no_review, "Do not require review");
    update_cmd->callback([&global, &exit_code, update_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, update_args->task, update_args->plan, update_args->line,
                       task)) {
            return;
        }
        auto* item = find_step(task, update_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + update_args->step);
            return;
        }
        if (item->locked) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + item->id + " is locked: " + item->lock_reason);
            return;
        }
        if (!update_args->title.empty()) item->title = update_args->title;
        if (!update_args->desc.empty()) item->desc = update_args->desc;
        if (update_args->review) item->requires_review = true;
        if (update_args->no_review) item->requires_review = false;
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.update", step_entity(task, *item), nullptr,
                         item->title, "update step");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Updated " + item->id + " in " + task.id,
                            {{"task", task.id},
                             {"step", item->id},
                             {"title", item->title},
                             {"requires_review", item->requires_review}});
    });

    auto delete_args = std::make_shared<SkipArgs>();
    auto* delete_cmd = step->add_subcommand("delete", "Delete a todo or skipped step");
    add_global_opts(delete_cmd, global);
    delete_cmd->add_option("task", delete_args->task, "Task id")->required();
    delete_cmd->add_option("step", delete_args->step, "Step id")->required();
    delete_cmd->add_option("--plan", delete_args->plan, "Plan id");
    delete_cmd->add_option("--line", delete_args->line, "Line id");
    delete_cmd->add_option("--reason", delete_args->reason, "Delete reason")->required();
    delete_cmd->callback([&global, &exit_code, delete_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, delete_args->task, delete_args->plan, delete_args->line,
                       task)) {
            return;
        }
        auto* item = find_step(task, delete_args->step);
        if (item == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + delete_args->step);
            return;
        }
        if (item->locked) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + item->id + " is locked: " + item->lock_reason);
            return;
        }
        if (item->status != "todo" && item->status != "skipped") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Only todo/skipped steps can be deleted, step is " + item->status);
            return;
        }
        const std::string removed = item->id;
        for (auto it = task.steps.begin(); it != task.steps.end(); ++it) {
            if (it->id != removed) continue;
            task.steps.erase(it);
            break;
        }
        if (task.current_step == removed) task.current_step.clear();
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "human", "step.delete",
                         {{"type", "step"},
                          {"plan", task.plan},
                          {"line", task.line},
                          {"task", task.id},
                          {"step", removed}},
                         removed, nullptr, delete_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Deleted " + removed + " from " + task.id,
                            {{"task", task.id}, {"step", removed}});
    });

    auto move_step_args = std::make_shared<MoveStepArgs>();
    auto* move_step_cmd = step->add_subcommand("move", "Move a todo step after another step");
    add_global_opts(move_step_cmd, global);
    move_step_cmd->add_option("task", move_step_args->task, "Task id")->required();
    move_step_cmd->add_option("step", move_step_args->step, "Step id")->required();
    move_step_cmd->add_option("--plan", move_step_args->plan, "Plan id");
    move_step_cmd->add_option("--line", move_step_args->line, "Line id");
    move_step_cmd->add_option("--after", move_step_args->after, "Move after this step id")
        ->required();
    move_step_cmd->callback([&global, &exit_code, move_step_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, move_step_args->task, move_step_args->plan,
                       move_step_args->line, task)) {
            return;
        }
        core::model::Step moved;
        bool found = false;
        for (const auto& item : task.steps) {
            if (item.id != move_step_args->step) continue;
            if (item.locked) {
                fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                     "Step " + item.id + " is locked: " + item.lock_reason);
                return;
            }
            if (item.status != "todo") {
                fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                     "Only todo steps can be moved, step is " + item.status);
                return;
            }
            moved = item;
            found = true;
        }
        if (!found) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Step not found: " + move_step_args->step);
            return;
        }
        bool anchor_found = false;
        for (const auto& item : task.steps) {
            if (item.id == move_step_args->after) anchor_found = true;
        }
        if (!anchor_found) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Anchor step not found: " + move_step_args->after);
            return;
        }
        for (auto it = task.steps.begin(); it != task.steps.end(); ++it) {
            if (it->id != moved.id) continue;
            task.steps.erase(it);
            break;
        }
        auto position = task.steps.begin();
        for (; position != task.steps.end(); ++position) {
            if (position->id == move_step_args->after) {
                ++position;
                break;
            }
        }
        task.steps.insert(position, moved);
        if (!save_task(*ctx, exit_code, task)) return;
        try {
            append_event(ctx->paths, "agent", "step.move", step_entity(task, moved),
                         move_step_args->step, move_step_args->after, "move step");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Moved " + moved.id + " after " + move_step_args->after,
                            {{"task", task.id},
                             {"step", moved.id},
                             {"after", move_step_args->after}});
    });

    auto list_args = std::make_shared<ListArgs>();
    auto* list_cmd = step->add_subcommand("list", "List steps of a task");
    add_global_opts(list_cmd, global);
    list_cmd->add_option("task", list_args->task, "Task id")->required();
    list_cmd->add_option("--plan", list_args->plan, "Plan id");
    list_cmd->add_option("--line", list_args->line, "Line id");
    list_cmd->callback([&global, &exit_code, list_args]() {
        const auto ctx = make_context(global);
        core::model::Task task;
        if (!load_task(*ctx, exit_code, list_args->task, list_args->plan, list_args->line, task)) {
            return;
        }
        nlohmann::json items = nlohmann::json::array();
        for (const auto& item : task.steps) {
            items.push_back({{"id", item.id},
                             {"title", item.title},
                             {"status", item.status},
                             {"requires_review", item.requires_review}});
        }
        ctx->out.print_both("Steps: " + std::to_string(items.size()),
                            {{"task", task.id}, {"current_step", task.current_step}, {"items", items}});
    });
}
