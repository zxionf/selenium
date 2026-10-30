#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/checkpoint.hpp"
#include "core/entity.hpp"
#include "core/fs.hpp"
#include "core/git.hpp"
#include "core/id.hpp"
#include "core/state.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

nlohmann::json task_entity(const core::model::Task& task) {
    return {{"type", "task"},
            {"plan", task.plan},
            {"line", task.line},
            {"task", task.id},
            {"uid", task.uid}};
}

bool find_task(Context& ctx, int& exit_code, const std::string& id, const std::string& plan,
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

std::vector<core::model::Checkpoint> load_checkpoints(const core::store::Paths& paths) {
    std::vector<core::model::Checkpoint> result;
    if (!core::fsys::exists(paths.checkpoints)) return result;
    for (const auto& month : std::filesystem::directory_iterator(paths.checkpoints)) {
        if (!month.is_directory()) continue;
        for (const auto& entry : std::filesystem::directory_iterator(month.path())) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            try {
                result.push_back(core::model::checkpoint_from_json(
                    nlohmann::json::parse(core::fsys::read_file(entry.path()))));
            } catch (...) {
            }
        }
    }
    return result;
}

bool find_checkpoint(const core::store::Paths& paths, const std::string& id,
                     core::model::Checkpoint& out) {
    for (const auto& checkpoint : load_checkpoints(paths)) {
        if (checkpoint.id != id) continue;
        out = checkpoint;
        return true;
    }
    return false;
}

nlohmann::json git_snapshot(const core::store::Paths& paths) {
    if (!core::git::present(paths.root)) return nlohmann::json::object();
    nlohmann::json snapshot = {{"head", core::git::head(paths)},
                               {"branch", core::git::branch(paths)}};
    nlohmann::json dirty = nlohmann::json::array();
    const auto status = core::git::run(paths.root, {"status", "--porcelain"});
    std::istringstream stream(status.output);
    std::string line;
    while (std::getline(stream, line)) {
        if (line.size() > 3) dirty.push_back(line.substr(3));
    }
    snapshot["dirty_files"] = dirty;
    return snapshot;
}

std::string next_task_id(const core::store::Paths& paths, const std::string& plan,
                         const std::string& line) {
    int highest = 0;
    for (const auto& task : core::store::load_tasks(paths)) {
        if (task.plan != plan || task.line != line) continue;
        if (task.id.size() < 2 || task.id[0] != 'T') continue;
        try {
            highest = std::max(highest, std::stoi(task.id.substr(1)));
        } catch (...) {
        }
    }
    return "T" + std::to_string(highest + 1);
}

bool acceptance_ok(const core::model::Task& task) {
    for (const auto& acceptance : task.acceptance) {
        const bool human = acceptance.verified_by.rfind("human", 0) == 0;
        if (acceptance.status != "pass") return false;
        if (task.review_policy == "strict" && !human) return false;
        if (task.review_policy == "mixed" && acceptance.requires_human && !human) return false;
    }
    return true;
}

bool transition_task(Context& ctx, int& exit_code, core::model::Task& task,
                     const std::string& to) {
    if (!core::state::task_transition(task.status, to)) {
        fail(ctx, exit_code, exit_codes::STATE_CONFLICT,
             "Task " + task.id + " cannot go from " + task.status + " to " + to);
        return false;
    }
    return true;
}

struct AddArgs {
    std::string title;
    std::string plan;
    std::string line;
    std::vector<std::string> modules;
    std::vector<std::string> depends;
    std::vector<std::string> acceptance;
    std::string review_policy{"mixed"};
    std::string actor{"agent"};
};

struct SelectArgs {
    std::string id;
    std::string plan;
    std::string line;
};

struct PauseArgs {
    std::string id;
    std::string plan;
    std::string line;
    std::string reason;
    std::string hint;
};

struct FailArgs {
    std::string id;
    std::string plan;
    std::string line;
    std::string reason;
};

struct VerifyArgs {
    std::string id;
    std::string plan;
    std::string line;
    std::string acceptance;
    std::string by{"human"};
};

struct ListArgs {
    std::string plan;
    std::string line;
    std::string status;
};

struct LocateArgs {
    std::string id;
    std::string plan;
    std::string line;
};

struct RevertArgs {
    std::string id;
    std::string plan;
    std::string line;
    std::string to;
    std::string reason{"revert to checkpoint"};
    bool with_git{false};
    bool exec{false};
    bool yes{false};
};

struct InsertArgs {
    std::string title;
    std::string line;
    std::string plan;
    std::string after;
    std::string before;
    std::vector<std::string> acceptance;
    bool pause_current{false};
};

struct MoveArgs {
    std::string id;
    std::string plan;
    std::string line;
    std::string to_line;
};

bool find_line_by_id(Context& ctx, int& exit_code, const std::string& plan,
                     const std::string& line_id, core::model::Line& out) {
    std::vector<core::model::Line> matches;
    try {
        for (const auto& line : core::store::load_lines(ctx.paths)) {
            if (line.id != line_id) continue;
            if (!plan.empty() && line.plan != plan) continue;
            matches.push_back(line);
        }
    } catch (const std::exception& error) {
        fail(ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        return false;
    }
    if (matches.empty()) {
        fail(ctx, exit_code, exit_codes::USAGE_ERROR, "Line not found: " + line_id);
        return false;
    }
    if (matches.size() > 1) {
        fail(ctx, exit_code, exit_codes::USAGE_ERROR, "Line id is ambiguous; pass --plan");
        return false;
    }
    out = matches.front();
    return true;
}

} // namespace

void register_task_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* task = app.add_subcommand("task", "Manage tasks");
    add_global_opts(task, global);

    auto add_args = std::make_shared<AddArgs>();
    auto* add_cmd = task->add_subcommand("add", "Add a task to an active line");
    add_global_opts(add_cmd, global);
    add_cmd->add_option("title", add_args->title, "Task title")->required();
    add_cmd->add_option("--line", add_args->line, "Line id")->required();
    add_cmd->add_option("--plan", add_args->plan, "Plan id");
    add_cmd->add_option("--modules", add_args->modules, "Allowed modules")
        ->delimiter(',');
    add_cmd->add_option("--depends", add_args->depends, "Depends on task id (repeatable)");
    add_cmd->add_option("--acceptance", add_args->acceptance,
                        "Acceptance description (repeatable)");
    add_cmd->add_option("--review-policy", add_args->review_policy, "strict|lenient|mixed")
        ->default_val("mixed")
        ->check(CLI::IsMember({"strict", "lenient", "mixed"}));
    add_cmd->add_option("--actor", add_args->actor, "Actor recorded in the event log")
        ->default_val("agent");
    add_cmd->callback([&global, &exit_code, add_args]() {
        const auto ctx = make_context(global);
        core::model::Line line;
        if (!find_line_by_id(*ctx, exit_code, add_args->plan, add_args->line, line)) return;
        if (line.status != "active") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Line " + line.id + " is " + line.status + ", expected active");
            return;
        }

        core::model::Task result;
        result.uid = core::id::new_task_uid();
        result.plan = line.plan;
        result.line = line.id;
        result.title = add_args->title;
        result.goal = add_args->title;
        result.status = "todo";
        result.review_policy = add_args->review_policy;
        result.created_at = core::model::now_utc();
        result.modules = add_args->modules.empty() ? line.modules : add_args->modules;

        int index = 0;
        for (const auto& desc : add_args->acceptance) {
            core::model::Acceptance item;
            item.id = "A" + std::to_string(++index);
            item.desc = desc;
            result.acceptance.push_back(item);
        }
        for (const auto& dep : add_args->depends) {
            std::string dep_line = line.id;
            std::string dep_id = dep;
            const auto dot = dep.find('.');
            if (dot != std::string::npos) {
                dep_line = dep.substr(0, dot);
                dep_id = dep.substr(dot + 1);
            }
            result.depends.push_back(
                {{"type", "task"}, {"plan", line.plan}, {"line", dep_line}, {"id", dep_id}});
        }

        try {
            result.id = next_task_id(ctx->paths, line.plan, line.id);
            core::store::save_task(ctx->paths, result);
            append_event(ctx->paths, add_args->actor, "task.add", task_entity(result), nullptr,
                         result.status, "create task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Added " + result.id + " to " + line.plan + "." + line.id,
                            {{"task", result.id}, {"uid", result.uid}, {"status", result.status}});
    });

    auto insert_args = std::make_shared<InsertArgs>();
    auto* insert_cmd = task->add_subcommand("insert", "Insert a task relative to another task");
    add_global_opts(insert_cmd, global);
    insert_cmd->add_option("title", insert_args->title, "Task title")->required();
    insert_cmd->add_option("--line", insert_args->line, "Line id")->required();
    insert_cmd->add_option("--plan", insert_args->plan, "Plan id");
    insert_cmd->add_option("--after", insert_args->after, "Insert after this task id");
    insert_cmd->add_option("--before", insert_args->before, "Insert before this task id");
    insert_cmd->add_option("--acceptance", insert_args->acceptance,
                           "Acceptance description (repeatable)");
    insert_cmd->add_flag("--pause-current", insert_args->pause_current,
                         "Pause the current in-progress task");
    insert_cmd->callback([&global, &exit_code, insert_args]() {
        const auto ctx = make_context(global);
        core::model::Line line;
        if (!find_line_by_id(*ctx, exit_code, insert_args->plan, insert_args->line, line)) return;
        if (line.status != "active") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Line " + line.id + " is " + line.status + ", expected active");
            return;
        }
        if (insert_args->after.empty() && insert_args->before.empty()) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Pass --after or --before");
            return;
        }

        std::vector<core::model::Task> all;
        try {
            all = core::store::load_tasks(ctx->paths);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        if (!insert_args->before.empty()) {
            bool found = false;
            for (const auto& candidate : all) {
                if (candidate.id == insert_args->before && candidate.plan == line.plan &&
                    candidate.line == line.id) {
                    found = true;
                }
            }
            if (!found) {
                fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                     "Anchor task not found: " + insert_args->before);
                return;
            }
        }

        core::model::Task inserted;
        inserted.uid = core::id::new_task_uid();
        inserted.plan = line.plan;
        inserted.line = line.id;
        inserted.title = insert_args->title;
        inserted.goal = insert_args->title;
        inserted.status = "todo";
        inserted.created_at = core::model::now_utc();
        inserted.modules = line.modules;
        int index = 0;
        for (const auto& desc : insert_args->acceptance) {
            core::model::Acceptance item;
            item.id = "A" + std::to_string(++index);
            item.desc = desc;
            inserted.acceptance.push_back(item);
        }
        if (!insert_args->after.empty()) {
            inserted.depends.push_back({{"type", "task"},
                                        {"plan", line.plan},
                                        {"line", line.id},
                                        {"id", insert_args->after}});
        }

        try {
            inserted.id = next_task_id(ctx->paths, line.plan, line.id);
            core::store::save_task(ctx->paths, inserted);
            if (!insert_args->before.empty()) {
                for (auto& candidate : core::store::load_tasks(ctx->paths)) {
                    if (candidate.id != insert_args->before) continue;
                    if (candidate.plan != line.plan || candidate.line != line.id) continue;
                    candidate.depends.push_back({{"type", "task"},
                                                 {"plan", line.plan},
                                                 {"line", line.id},
                                                 {"id", inserted.id}});
                    core::store::save_task(ctx->paths, candidate);
                }
            }
            if (insert_args->pause_current) {
                for (auto& candidate : core::store::load_tasks(ctx->paths)) {
                    if (candidate.status != "in_progress") continue;
                    if (!core::state::task_transition(candidate.status, "paused")) continue;
                    candidate.status = "paused";
                    candidate.pause_context = {{"at", core::model::now_utc()},
                                               {"reason", "inserted " + inserted.id},
                                               {"current_step", candidate.current_step.empty()
                                                                    ? nlohmann::json(nullptr)
                                                                    : nlohmann::json(candidate.current_step)},
                                               {"resume_hint", ""}};
                    core::store::save_task(ctx->paths, candidate);
                    append_event(ctx->paths, "agent", "task.pause", task_entity(candidate),
                                 "in_progress", "paused", "pause on insert");
                }
            }
            append_event(ctx->paths, "agent", "task.insert", task_entity(inserted), nullptr,
                         inserted.status, "insert task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Inserted " + inserted.id + " into " + line.plan + "." + line.id,
                            {{"task", inserted.id},
                             {"uid", inserted.uid},
                             {"status", inserted.status}});
    });

    auto start_args = std::make_shared<SelectArgs>();
    auto* start_cmd = task->add_subcommand("start", "Start a task (auto-pauses the current one)");
    add_global_opts(start_cmd, global);
    start_cmd->add_option("id", start_args->id, "Task id")->required();
    start_cmd->add_option("--plan", start_args->plan, "Plan id");
    start_cmd->add_option("--line", start_args->line, "Line id");
    start_cmd->callback([&global, &exit_code, start_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, start_args->id, start_args->plan, start_args->line, item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "in_progress")) return;

        try {
            for (const auto& dep : item.depends) {
                if (dep.value("type", "") != "task") continue;
                const std::string dep_id = dep.value("id", "");
                const std::string dep_plan = dep.value("plan", item.plan);
                const std::string dep_line = dep.value("line", item.line);
                std::vector<core::model::Task> matches;
                for (const auto& candidate : core::store::load_tasks(ctx->paths)) {
                    if (candidate.id == dep_id && candidate.plan == dep_plan &&
                        candidate.line == dep_line) {
                        matches.push_back(candidate);
                    }
                }
                if (matches.empty() || (matches.front().status != "done" &&
                                        matches.front().status != "archived")) {
                    fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                         "Dependency " + dep_id + " is not done");
                    return;
                }
            }

            for (auto& candidate : core::store::load_tasks(ctx->paths)) {
                if (candidate.uid == item.uid || candidate.status != "in_progress") continue;
                if (!core::state::task_transition(candidate.status, "paused")) continue;
                candidate.status = "paused";
                candidate.pause_context = {{"at", core::model::now_utc()},
                                           {"reason", "switched to " + item.id},
                                           {"current_step", candidate.current_step.empty()
                                                                ? nlohmann::json(nullptr)
                                                                : nlohmann::json(candidate.current_step)},
                                           {"resume_hint", ""},
                                           {"git_snapshot", git_snapshot(ctx->paths)}};
                core::store::save_task(ctx->paths, candidate);
                append_event(ctx->paths, "agent", "task.pause", task_entity(candidate),
                             "in_progress", "paused", "auto pause on switch");
            }
            for (auto& candidate : core::store::load_tasks(ctx->paths)) {
                if (candidate.uid == item.uid) continue;
                if (candidate.status != "paused") continue;
                ++candidate.skipped_count;
                core::store::save_task(ctx->paths, candidate);
                append_event(ctx->paths, "agent", "task.skip", task_entity(candidate), nullptr,
                             candidate.skipped_count, "skipped by start " + item.id);
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }

        item.status = "in_progress";
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "agent", "task.start", task_entity(item), "todo",
                         item.status, "start task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Started " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto pause_args = std::make_shared<PauseArgs>();
    auto* pause_cmd = task->add_subcommand("pause", "Pause an in-progress task");
    add_global_opts(pause_cmd, global);
    pause_cmd->add_option("id", pause_args->id, "Task id")->required();
    pause_cmd->add_option("--plan", pause_args->plan, "Plan id");
    pause_cmd->add_option("--line", pause_args->line, "Line id");
    pause_cmd->add_option("--reason", pause_args->reason, "Pause reason")->required();
    pause_cmd->add_option("--hint", pause_args->hint, "Resume hint");
    pause_cmd->callback([&global, &exit_code, pause_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, pause_args->id, pause_args->plan, pause_args->line, item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "paused")) return;
        item.status = "paused";
        item.pause_context = {{"at", core::model::now_utc()},
                              {"reason", pause_args->reason},
                              {"current_step", item.current_step.empty()
                                                   ? nlohmann::json(nullptr)
                                                   : nlohmann::json(item.current_step)},
                              {"resume_hint", pause_args->hint},
                              {"git_snapshot", git_snapshot(ctx->paths)}};
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "agent", "task.pause", task_entity(item), "in_progress",
                         item.status, pause_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Paused " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto keep_args = std::make_shared<SelectArgs>();
    auto* keep_cmd = task->add_subcommand("keep", "Keep a paused task and reset its skip count");
    add_global_opts(keep_cmd, global);
    keep_cmd->add_option("id", keep_args->id, "Task id")->required();
    keep_cmd->add_option("--plan", keep_args->plan, "Plan id");
    keep_cmd->add_option("--line", keep_args->line, "Line id");
    keep_cmd->callback([&global, &exit_code, keep_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, keep_args->id, keep_args->plan, keep_args->line, item)) {
            return;
        }
        item.skipped_count = 0;
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "human", "task.keep", task_entity(item), nullptr, 0,
                         "reset skip count");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Kept " + item.id + " (skip count reset)",
                            {{"task", item.id}, {"skipped_count", 0}});
    });

    auto resume_args = std::make_shared<SelectArgs>();
    auto* resume_cmd = task->add_subcommand("resume", "Resume a paused task");
    add_global_opts(resume_cmd, global);
    resume_cmd->add_option("id", resume_args->id, "Task id")->required();
    resume_cmd->add_option("--plan", resume_args->plan, "Plan id");
    resume_cmd->add_option("--line", resume_args->line, "Line id");
    resume_cmd->callback([&global, &exit_code, resume_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, resume_args->id, resume_args->plan, resume_args->line,
                       item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "in_progress")) return;
        item.status = "in_progress";
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "agent", "task.resume", task_entity(item), "paused",
                         item.status, "resume task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Resumed " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto done_args = std::make_shared<SelectArgs>();
    auto* done_cmd = task->add_subcommand("done", "Finish a task");
    add_global_opts(done_cmd, global);
    done_cmd->add_option("id", done_args->id, "Task id")->required();
    done_cmd->add_option("--plan", done_args->plan, "Plan id");
    done_cmd->add_option("--line", done_args->line, "Line id");
    done_cmd->callback([&global, &exit_code, done_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, done_args->id, done_args->plan, done_args->line, item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "done")) return;
        for (const auto& step : item.steps) {
            if (step.status != "done" && step.status != "skipped") {
                fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                     "Step " + step.id + " is " + step.status);
                return;
            }
        }
        bool has_checkpoint = false;
        for (const auto& checkpoint : load_checkpoints(ctx->paths)) {
            if (checkpoint.entity.value("task", "") != item.id) continue;
            has_checkpoint = true;
            break;
        }
        if (!has_checkpoint) {
            try {
                const auto checkpoint = core::checkpoint::create_task(ctx->paths, item);
                append_event(ctx->paths, "agent", "checkpoint.create", checkpoint.entity,
                             nullptr, checkpoint.id, "auto checkpoint on done");
            } catch (const std::exception& error) {
                fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
                return;
            }
        }
        const bool ready = acceptance_ok(item);
        item.status = ready ? "done" : "pending_review";
        if (ready) item.finished_at = core::model::now_utc();
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "agent", "task.done", task_entity(item), "in_progress",
                         item.status, ready ? "task done" : "waiting for human review");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both(ready ? "Finished " + item.id
                                  : "Submitted " + item.id + " for review",
                            {{"task", item.id}, {"status", item.status}});
    });

    auto approve_args = std::make_shared<SelectArgs>();
    auto* approve_cmd = task->add_subcommand("approve", "Approve a pending task");
    add_global_opts(approve_cmd, global);
    approve_cmd->add_option("id", approve_args->id, "Task id")->required();
    approve_cmd->add_option("--plan", approve_args->plan, "Plan id");
    approve_cmd->add_option("--line", approve_args->line, "Line id");
    approve_cmd->callback([&global, &exit_code, approve_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, approve_args->id, approve_args->plan, approve_args->line,
                       item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "done")) return;
        item.status = "done";
        item.finished_at = core::model::now_utc();
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "human", "task.approve", task_entity(item),
                         "pending_review", item.status, "approved");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Approved " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto fail_args = std::make_shared<FailArgs>();
    auto* fail_cmd = task->add_subcommand("fail", "Mark a task failed");
    add_global_opts(fail_cmd, global);
    fail_cmd->add_option("id", fail_args->id, "Task id")->required();
    fail_cmd->add_option("--plan", fail_args->plan, "Plan id");
    fail_cmd->add_option("--line", fail_args->line, "Line id");
    fail_cmd->add_option("--reason", fail_args->reason, "Failure reason")->required();
    fail_cmd->callback([&global, &exit_code, fail_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, fail_args->id, fail_args->plan, fail_args->line, item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "failed")) return;
        item.status = "failed";
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "agent", "task.fail", task_entity(item), "in_progress",
                         item.status, fail_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Failed " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto retry_args = std::make_shared<SelectArgs>();
    auto* retry_cmd = task->add_subcommand("retry", "Retry a failed task");
    add_global_opts(retry_cmd, global);
    retry_cmd->add_option("id", retry_args->id, "Task id")->required();
    retry_cmd->add_option("--plan", retry_args->plan, "Plan id");
    retry_cmd->add_option("--line", retry_args->line, "Line id");
    retry_cmd->callback([&global, &exit_code, retry_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, retry_args->id, retry_args->plan, retry_args->line, item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "in_progress")) return;
        item.status = "in_progress";
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "agent", "task.retry", task_entity(item), "failed",
                         item.status, "retry task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Retried " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto block_args = std::make_shared<FailArgs>();
    auto* block_cmd = task->add_subcommand("block", "Block a task");
    add_global_opts(block_cmd, global);
    block_cmd->add_option("id", block_args->id, "Task id")->required();
    block_cmd->add_option("--plan", block_args->plan, "Plan id");
    block_cmd->add_option("--line", block_args->line, "Line id");
    block_cmd->add_option("--reason", block_args->reason, "Block reason")->required();
    block_cmd->callback([&global, &exit_code, block_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, block_args->id, block_args->plan, block_args->line, item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "blocked")) return;
        item.status = "blocked";
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "agent", "task.block", task_entity(item), "in_progress",
                         item.status, block_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Blocked " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto unblock_args = std::make_shared<SelectArgs>();
    auto* unblock_cmd = task->add_subcommand("unblock", "Unblock a task");
    add_global_opts(unblock_cmd, global);
    unblock_cmd->add_option("id", unblock_args->id, "Task id")->required();
    unblock_cmd->add_option("--plan", unblock_args->plan, "Plan id");
    unblock_cmd->add_option("--line", unblock_args->line, "Line id");
    unblock_cmd->callback([&global, &exit_code, unblock_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, unblock_args->id, unblock_args->plan, unblock_args->line,
                       item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "in_progress")) return;
        item.status = "in_progress";
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "human", "task.unblock", task_entity(item), "blocked",
                         item.status, "unblock");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Unblocked " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto verify_args = std::make_shared<VerifyArgs>();
    auto* verify_cmd = task->add_subcommand("verify", "Verify a task acceptance point");
    add_global_opts(verify_cmd, global);
    verify_cmd->add_option("id", verify_args->id, "Task id")->required();
    verify_cmd->add_option("acceptance", verify_args->acceptance, "Acceptance id")->required();
    verify_cmd->add_option("--plan", verify_args->plan, "Plan id");
    verify_cmd->add_option("--line", verify_args->line, "Line id");
    verify_cmd->add_option("--by", verify_args->by, "Verifier identity")->default_val("human");
    verify_cmd->callback([&global, &exit_code, verify_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, verify_args->id, verify_args->plan, verify_args->line,
                       item)) {
            return;
        }
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
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, verify_args->by, "task.verify", task_entity(item),
                         "unverified", "pass", verify_args->acceptance);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Verified " + verify_args->acceptance + " on " + item.id,
                            {{"task", item.id}, {"acceptance", verify_args->acceptance}});
    });

    auto locate_args = std::make_shared<LocateArgs>();
    auto* locate_cmd = task->add_subcommand("locate", "Locate a task file");
    add_global_opts(locate_cmd, global);
    locate_cmd->add_option("id", locate_args->id, "Task id")->required();
    locate_cmd->add_option("--plan", locate_args->plan, "Plan id");
    locate_cmd->add_option("--line", locate_args->line, "Line id");
    locate_cmd->callback([&global, &exit_code, locate_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, locate_args->id, locate_args->plan, locate_args->line,
                       item)) {
            return;
        }
        const auto path = core::store::task_path(ctx->paths, item.uid);
        const auto relative =
            std::filesystem::relative(path, ctx->paths.sele).generic_string();
        ctx->out.print_both(relative,
                            {{"task", item.id},
                             {"uid", item.uid},
                             {"file", relative},
                             {"plan", item.plan},
                             {"line", item.line}});
    });

    auto revert_args = std::make_shared<RevertArgs>();
    auto* revert_cmd = task->add_subcommand("revert", "Revert a task to a checkpoint (sele-only)");
    add_global_opts(revert_cmd, global);
    revert_cmd->add_option("id", revert_args->id, "Task id")->required();
    revert_cmd->add_option("--to", revert_args->to, "Checkpoint id")->required();
    revert_cmd->add_option("--plan", revert_args->plan, "Plan id");
    revert_cmd->add_option("--line", revert_args->line, "Line id");
    revert_cmd->add_option("--reason", revert_args->reason, "Decision reason");
    revert_cmd->add_flag("--with-git", revert_args->with_git, "Print the git reset command");
    revert_cmd->add_flag("--exec", revert_args->exec,
                         "Execute git reset --hard (requires --yes)");
    revert_cmd->add_flag("--yes", revert_args->yes, "Confirm the destructive reset");
    revert_cmd->callback([&global, &exit_code, revert_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, revert_args->id, revert_args->plan, revert_args->line,
                       item)) {
            return;
        }
        core::model::Checkpoint target;
        if (!find_checkpoint(ctx->paths, revert_args->to, target)) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Checkpoint not found: " + revert_args->to);
            return;
        }
        if (target.entity.value("task", "") != item.id) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Checkpoint " + target.id + " belongs to another task");
            return;
        }
        if (revert_args->exec) {
            if (!revert_args->yes) {
                fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                     "Pass --yes to confirm git reset --hard");
                return;
            }
            const auto status = core::git::run(
                ctx->paths.root,
                {"status", "--porcelain", "--", ".", ":(exclude).sele"});
            if (!status.output.empty()) {
                fail(*ctx, exit_code, exit_codes::ENV_ERROR,
                     "Working tree is not clean; commit or stash before --exec");
                return;
            }
            const std::string commit = target.git.value("commit", "");
            if (commit.empty()) {
                fail(*ctx, exit_code, exit_codes::ENV_ERROR, "Checkpoint has no commit");
                return;
            }
            const auto reset = core::git::run(ctx->paths.root, {"reset", "--hard", commit});
            if (reset.code != 0) {
                fail(*ctx, exit_code, exit_codes::ENV_ERROR, "git reset failed: " + reset.output);
                return;
            }
            ctx->out.print_both("Reset to " + commit + " and reverted " + item.id,
                                {{"task", item.id},
                                 {"checkpoint", target.id},
                                 {"commit", commit}});
            return;
        }

        const std::string step_id = target.entity.value("step", "");
        int index = -1;
        for (int position = 0; position < static_cast<int>(item.steps.size()); ++position) {
            if (item.steps[static_cast<std::size_t>(position)].id == step_id) index = position;
        }
        if (index < 0) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Checkpoint step not found in task: " + step_id);
            return;
        }

        for (int position = 0; position < static_cast<int>(item.steps.size()); ++position) {
            auto& step = item.steps[static_cast<std::size_t>(position)];
            if (position > index) {
                step.status = "todo";
                step.checkpoint.clear();
                step.skipped_reason.clear();
            } else if (position == index) {
                step.status = "in_progress";
            }
        }
        item.status = "in_progress";
        item.current_step = step_id;
        item.finished_at.clear();

        try {
            for (auto& checkpoint : load_checkpoints(ctx->paths)) {
                if (checkpoint.entity.value("task", "") != item.id) continue;
                if (checkpoint.id <= target.id) continue;
                checkpoint.status = "invalidated";
                core::store::save_checkpoint(ctx->paths, checkpoint);
            }
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "human", "task.revert", task_entity(item),
                         item.status, target.id, revert_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }

        if (revert_args->with_git) {
            ctx->out.print_text("Run: git -C " + ctx->paths.root.string() + " reset --hard " +
                                target.git.value("commit", ""));
        }
        ctx->out.print_both("Reverted " + item.id + " to " + target.id,
                            {{"task", item.id},
                             {"checkpoint", target.id},
                             {"status", item.status},
                             {"step", step_id}});
    });

    auto list_args = std::make_shared<ListArgs>();
    auto* list_cmd = task->add_subcommand("list", "List tasks");
    add_global_opts(list_cmd, global);
    list_cmd->add_option("--plan", list_args->plan, "Filter by plan");
    list_cmd->add_option("--line", list_args->line, "Filter by line");
    list_cmd->add_option("--status", list_args->status, "Filter by status");
    list_cmd->callback([&global, &exit_code, list_args]() {
        const auto ctx = make_context(global);
        try {
            nlohmann::json items = nlohmann::json::array();
            for (const auto& item : core::store::load_tasks(ctx->paths)) {
                if (!list_args->plan.empty() && item.plan != list_args->plan) continue;
                if (!list_args->line.empty() && item.line != list_args->line) continue;
                if (!list_args->status.empty() && item.status != list_args->status) continue;
                items.push_back({{"id", item.id},
                                 {"uid", item.uid},
                                 {"plan", item.plan},
                                 {"line", item.line},
                                 {"title", item.title},
                                 {"status", item.status},
                                 {"skipped_count", item.skipped_count},
                                 {"steps", item.steps.size()}});
            }
            ctx->out.print_both("Tasks: " + std::to_string(items.size()),
                                {{"command", "task.list"}, {"items", items}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto move_args = std::make_shared<MoveArgs>();
    auto* move_cmd = task->add_subcommand("move", "Move a task to another line");
    add_global_opts(move_cmd, global);
    move_cmd->add_option("id", move_args->id, "Task id")->required();
    move_cmd->add_option("--to-line", move_args->to_line, "Target line id")->required();
    move_cmd->add_option("--plan", move_args->plan, "Target plan id");
    move_cmd->add_option("--line", move_args->line, "Source line id");
    move_cmd->callback([&global, &exit_code, move_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, move_args->id, "", move_args->line, item)) return;
        const std::string target_plan = move_args->plan.empty() ? item.plan : move_args->plan;
        core::model::Line target;
        if (!find_line_by_id(*ctx, exit_code, target_plan, move_args->to_line, target)) return;
        if (target.status != "active") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Target line " + target.id + " is " + target.status + ", expected active");
            return;
        }
        if (target.plan == item.plan && target.id == item.line) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Task is already in that line");
            return;
        }
        try {
            for (const auto& candidate : core::store::load_tasks(ctx->paths)) {
                if (candidate.plan == target.plan && candidate.line == target.id &&
                    candidate.id == item.id) {
                    fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                         "Task id already exists in target line: " + item.id);
                    return;
                }
            }
            item.plan = target.plan;
            item.line = target.id;
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "human", "task.move", task_entity(item), nullptr,
                         item.plan + "." + item.line, "move task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Moved " + item.id + " to " + item.plan + "." + item.line,
                            {{"task", item.id},
                             {"plan", item.plan},
                             {"line", item.line}});
    });

    auto archive_args = std::make_shared<SelectArgs>();
    auto* archive_cmd = task->add_subcommand("archive", "Archive a done task");
    add_global_opts(archive_cmd, global);
    archive_cmd->add_option("id", archive_args->id, "Task id")->required();
    archive_cmd->add_option("--plan", archive_args->plan, "Plan id");
    archive_cmd->add_option("--line", archive_args->line, "Line id");
    archive_cmd->callback([&global, &exit_code, archive_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, archive_args->id, archive_args->plan, archive_args->line,
                       item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "archived")) return;
        item.status = "archived";
        item.archived_at = core::model::now_utc();
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "human", "task.archive", task_entity(item), "done",
                         item.status, "archive task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Archived " + item.id, {{"task", item.id}, {"status", item.status}});
    });

    auto revive_args = std::make_shared<SelectArgs>();
    auto* revive_cmd = task->add_subcommand("revive", "Revive an archived task");
    add_global_opts(revive_cmd, global);
    revive_cmd->add_option("id", revive_args->id, "Task id")->required();
    revive_cmd->add_option("--plan", revive_args->plan, "Plan id");
    revive_cmd->add_option("--line", revive_args->line, "Line id");
    revive_cmd->callback([&global, &exit_code, revive_args]() {
        const auto ctx = make_context(global);
        core::model::Task item;
        if (!find_task(*ctx, exit_code, revive_args->id, revive_args->plan, revive_args->line,
                       item)) {
            return;
        }
        if (!transition_task(*ctx, exit_code, item, "todo")) return;
        item.status = "todo";
        item.archived_at.clear();
        item.archived_git_head.clear();
        try {
            core::store::save_task(ctx->paths, item);
            append_event(ctx->paths, "human", "task.revive", task_entity(item), "archived",
                         item.status, "revive task");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Revived " + item.id, {{"task", item.id}, {"status", item.status}});
    });
}
