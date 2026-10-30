#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/state.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
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
    try {
        for (const auto& task : core::store::load_tasks(ctx.paths)) {
            if (task.id != id) continue;
            if (!plan.empty() && task.plan != plan) continue;
            if (!line.empty() && task.line != line) continue;
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
    std::string checkpoint;
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
    done_cmd->add_option("--checkpoint", done_args->checkpoint, "Checkpoint id");
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
        const std::string target = item->requires_review ? "pending_review" : "done";
        if (!transition_step(*ctx, exit_code, *item, target)) return;
        item->status = target;
        if (!done_args->checkpoint.empty()) item->checkpoint = done_args->checkpoint;
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
