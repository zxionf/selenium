#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "cli/nextgen.hpp"
#include "core/fs.hpp"
#include "core/gitrt.hpp"
#include "core/model.hpp"
#include "core/sele.hpp"
#include "core/session.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace cli {

namespace {

using ng::fail;

bool valid_module(const std::string& module) {
    static const std::regex pattern("^[a-z][a-z0-9-]*$");
    return std::regex_match(module, pattern);
}

std::string branch_for(const std::string& line_id) {
    return "sele/" + line_id;
}

std::string task_line(const std::string& id) {
    return "sele task " + id;
}

void add_task_options(CLI::App* sub, GlobalOptions& global_opts) {
    add_global_opts(sub, global_opts);
}

// Transition helper: target state already reached -> idempotent success.
bool already(const ng::Ctx& ctx, const std::string& task_id, const std::string& desired) {
    return ng::status_of(ctx, task_id) == desired;
}

void require_task(const ng::Ctx& ctx, const std::string& task_id) {
    if (!ctx.has_task(task_id)) {
        fail(exit_codes::STATE_CONFLICT, "Task not found: " + task_id,
             "List tasks for this line", {"sele task list --line " + ctx.line_id});
    }
}

} // namespace

void register_task(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto* group = app.add_subcommand("task", "Manage tasks inside a line");
    group->require_subcommand(1);

    // task add
    struct AddArgs {
        std::string title;
        std::string line;
        std::string modules;
        std::string depends;
    };
    auto add_args = std::make_shared<AddArgs>();
    auto* cmd_add = group->add_subcommand("add", "Append a task to the line chain");
    add_task_options(cmd_add, global_opts);
    cmd_add->add_option("title", add_args->title, "Task title")->required();
    cmd_add->add_option("--line", add_args->line, "Line id (default: session pointer)");
    cmd_add->add_option("--modules", add_args->modules, "Comma-separated module tags")->required();
    cmd_add->add_option("--depends", add_args->depends, "Explicit predecessor (default: chain tail)");
    cmd_add->footer("Example:\n  sele task add \"Reproduce 401 branch\" --modules auth\n"
                    "  sele task add \"Add docs\" --modules docs --line 20261029-fix-login");
    cmd_add->callback([&global_opts, &exit_code, add_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, add_args->line);
            if (ctx.line.status == "done" || ctx.line.status == "abandoned" ||
                ctx.line.status == "merged") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + " and cannot take new tasks",
                     "Reopen the line first", {"sele line show " + ctx.line_id});
            }
            if (add_args->title.empty()) {
                fail(exit_codes::USAGE_ERROR, "Task title must not be empty", "");
            }
            const auto modules = ng::split_csv(add_args->modules);
            if (modules.empty()) {
                fail(exit_codes::USAGE_ERROR, "--modules requires at least one tag", "");
            }
            for (const auto& module : modules) {
                if (!valid_module(module)) {
                    fail(exit_codes::USAGE_ERROR, "Invalid module tag: " + module,
                         "Module tags use [a-z][a-z0-9-]*");
                }
            }

            core::TaskSpec task;
            task.id = ng::next_task_id(ctx);
            task.title = add_args->title;
            task.modules = modules;
            task.created_at = core::sele::utc_now();
            task.created_by = ctx.actor;

            const std::string tail = ng::last_task_id(ctx);
            if (!add_args->depends.empty()) {
                if (add_args->depends != tail) {
                    fail(exit_codes::USAGE_ERROR,
                         "--depends must point at the chain tail (" + (tail.empty() ? "none" : tail) +
                             ")",
                         "Omit --depends to append at the tail");
                }
                task.depends = {add_args->depends};
            } else if (!tail.empty()) {
                task.depends = {tail};
            }

            ng::save_task(ctx, task);
            ng::append_event(ctx, "task.add", {{"task", task.id}});

            nlohmann::json result;
            result["command"] = "task.add";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["task"] = task.id;
            result["depends"] = task.depends;
            result["path"] = (core::sele::tasks_dir(ctx.lay, ctx.line_id) / (task.id + ".json")).string();

            std::ostringstream text;
            text << "Added " << task.id << " to " << ctx.line_id << ": " << task.title;
            if (!task.depends.empty()) text << " (depends: " << task.depends.front() << ')';
            output.print_both(text.str(), result);
        });
    });

    // task list
    struct ListArgs {
        std::string line;
    };
    auto list_args = std::make_shared<ListArgs>();
    auto* cmd_list = group->add_subcommand("list", "List tasks and their state");
    add_task_options(cmd_list, global_opts);
    cmd_list->add_option("--line", list_args->line, "Line id (default: session pointer)");
    cmd_list->footer("Example:\n  sele task list\n  sele task list --line 20261029-fix-login");
    cmd_list->callback([&global_opts, &exit_code, list_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, list_args->line);
            nlohmann::json items = nlohmann::json::array();
            std::ostringstream text;
            text << "Line " << ctx.line_id << " [" << ctx.line.status << "]";

            for (const auto& task : ctx.tasks) {
                const auto& state = ctx.task_state(task.id);
                items.push_back({{"id", task.id},
                                 {"title", task.title},
                                 {"status", ng::status_of(ctx, task.id)},
                                 {"modules", task.modules},
                                 {"depends", task.depends},
                                 {"checkpoint", state.checkpoint}});
                text << "\n  " << task.id << " [" << ng::status_of(ctx, task.id) << "] " << task.title;
                if (!task.modules.empty()) text << " (modules: " << ng::join(task.modules, ", ") << ')';
            }
            if (ctx.tasks.empty()) text << "\n  (no tasks)";

            nlohmann::json result;
            result["command"] = "task.list";
            result["line"] = ctx.line_id;
            result["items"] = items;
            output.print_both(text.str(), result);
        });
    });

    // task start
    struct TaskArgs {
        std::string task;
        std::string line;
    };
    auto start_args = std::make_shared<TaskArgs>();
    auto* cmd_start = group->add_subcommand("start", "Start a pending task");
    add_task_options(cmd_start, global_opts);
    cmd_start->add_option("task", start_args->task, "Task id (T1, T2, ...)")->required();
    cmd_start->add_option("--line", start_args->line, "Line id (default: session pointer)");
    cmd_start->footer("Example:\n  sele task start T1\n  sele task start T1 --line 20261029-fix-login");
    cmd_start->callback([&global_opts, &exit_code, start_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, start_args->line);
            ng::require_active(ctx, "task start");
            require_task(ctx, start_args->task);

            if (already(ctx, start_args->task, "running")) {
                nlohmann::json result;
                result["command"] = "task.start";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["task"] = start_args->task;
                result["status"] = "running";
                output.print_both("Task " + start_args->task + " is already running (unchanged)", result);
                return;
            }
            const std::string current = ng::status_of(ctx, start_args->task);
            if (current != "pending") {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + start_args->task + " is " + current + ", not pending",
                     "Only pending tasks can be started", {task_line("list")});
            }
            for (const auto& task : ctx.tasks) {
                if (task.id != start_args->task && ng::status_of(ctx, task.id) == "running") {
                    fail(exit_codes::STATE_CONFLICT,
                         "Another task is already running: " + task.id,
                         "v0.3 allows one running task per line",
                         {"sele task done " + ctx.line_id + " " + task.id});
                }
            }

            auto& state = ctx.task_state(start_args->task);
            state.status = "running";
            state.started_at = core::sele::utc_now();
            ng::save_runtime(ctx);

            core::session::Pointer pointer;
            pointer.line = ctx.line_id;
            pointer.task = start_args->task;
            core::session::write(ctx.lay, ctx.session, pointer);
            ng::append_event(ctx, "task.start", {{"task", start_args->task}});

            nlohmann::json result;
            result["command"] = "task.start";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["task"] = start_args->task;
            result["status"] = "running";
            output.print_both("Started " + start_args->task + " in " + ctx.line_id, result);
        });
    });

    // task checkpoint
    struct CheckpointArgs {
        std::string task;
        std::string line;
        std::string msg;
    };
    auto checkpoint_args = std::make_shared<CheckpointArgs>();
    auto* cmd_checkpoint = group->add_subcommand("checkpoint", "Commit a snapshot for a running task");
    add_task_options(cmd_checkpoint, global_opts);
    cmd_checkpoint->add_option("task", checkpoint_args->task, "Task id")->required();
    cmd_checkpoint->add_option("--line", checkpoint_args->line, "Line id (default: session pointer)");
    cmd_checkpoint->add_option("--msg", checkpoint_args->msg, "Commit message");
    cmd_checkpoint->footer("Example:\n  sele task checkpoint T1 --msg \"wip: auth flow\"");
    cmd_checkpoint->callback([&global_opts, &exit_code, checkpoint_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, checkpoint_args->line);
            ng::require_active(ctx, "task checkpoint");
            require_task(ctx, checkpoint_args->task);

            const std::string current = ng::status_of(ctx, checkpoint_args->task);
            if (current != "running") {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + checkpoint_args->task + " is " + current + ", not running",
                     "Start the task before checkpointing",
                     {"sele task start " + ctx.line_id + " " + checkpoint_args->task});
            }
            if (!core::gitrt::is_repo(ctx.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Not a git repository: " + ctx.lay.root.string(),
                     "Run git init first");
            }
            const std::string branch = branch_for(ctx.line_id);
            const std::string branch_now = core::gitrt::current_branch(ctx.lay.root);
            if (branch_now != branch) {
                fail(exit_codes::STATE_CONFLICT,
                     "Current branch is '" + branch_now + "', expected '" + branch + "'",
                     "Switch to the line branch first", {"sele line switch " + ctx.line_id});
            }

            const auto* spec = ctx.task(checkpoint_args->task);
            const std::string message =
                checkpoint_args->msg.empty()
                    ? ctx.line_id + ": " + spec->id + " " + spec->title
                    : checkpoint_args->msg;
            const bool committed = core::gitrt::commit_all(ctx.lay.root, message);
            auto& state = ctx.task_state(checkpoint_args->task);
            const std::string sha = core::gitrt::head_short(ctx.lay.root);
            if (committed) state.checkpoint = sha;
            if (committed || !state.checkpoint.empty()) ng::save_runtime(ctx);
            if (committed) {
                ng::append_event(ctx, "task.checkpoint",
                                 {{"task", checkpoint_args->task}, {"commit", sha}});
            }

            nlohmann::json result;
            result["command"] = "task.checkpoint";
            result["changed"] = committed;
            result["line"] = ctx.line_id;
            result["task"] = checkpoint_args->task;
            result["commit"] = state.checkpoint;
            result["committed"] = committed;

            std::ostringstream text;
            if (committed) {
                text << "Checkpointed " << checkpoint_args->task << " at " << sha;
            } else if (!state.checkpoint.empty()) {
                text << "No changes to commit; reusing checkpoint " << state.checkpoint;
            } else {
                text << "No changes to commit; task " << checkpoint_args->task
                     << " has no checkpoint yet";
            }
            output.print_both(text.str(), result);
        });
    });

    // task done
    auto done_args = std::make_shared<TaskArgs>();
    auto* cmd_done = group->add_subcommand("done", "Mark a running task done");
    add_task_options(cmd_done, global_opts);
    cmd_done->add_option("task", done_args->task, "Task id")->required();
    cmd_done->add_option("--line", done_args->line, "Line id (default: session pointer)");
    cmd_done->footer("Example:\n  sele task done T1");
    cmd_done->callback([&global_opts, &exit_code, done_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, done_args->line);
            ng::require_active(ctx, "task done");
            require_task(ctx, done_args->task);

            if (already(ctx, done_args->task, "done")) {
                nlohmann::json result;
                result["command"] = "task.done";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["task"] = done_args->task;
                result["status"] = "done";
                output.print_both("Task " + done_args->task + " is already done (unchanged)", result);
                return;
            }
            const std::string current = ng::status_of(ctx, done_args->task);
            if (current != "running") {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + done_args->task + " is " + current + ", not running",
                     "Start the task before finishing it",
                     {"sele task start " + ctx.line_id + " " + done_args->task});
            }
            auto& state = ctx.task_state(done_args->task);
            if (state.checkpoint.empty()) {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + done_args->task + " has no checkpoint",
                     "Create one before marking it done",
                     {"sele task checkpoint " + ctx.line_id + " " + done_args->task});
            }
            if (core::gitrt::is_repo(ctx.lay.root) && core::gitrt::is_dirty(ctx.lay.root)) {
                fail(exit_codes::STATE_CONFLICT, "Working tree has uncommitted code changes",
                     "Checkpoint them before marking the task done",
                     {"sele task checkpoint " + ctx.line_id + " " + done_args->task});
            }

            state.status = "done";
            state.finished_at = core::sele::utc_now();
            ng::save_runtime(ctx);
            ng::append_log(ctx, "task.done",
                           {{"task", done_args->task}, {"commit", state.checkpoint}});
            ng::append_event(ctx, "task.done", {{"task", done_args->task}});

            nlohmann::json result;
            result["command"] = "task.done";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["task"] = done_args->task;
            result["status"] = "done";
            result["commit"] = state.checkpoint;
            output.print_both("Done " + done_args->task + " in " + ctx.line_id + " (" +
                                  state.checkpoint + ")",
                              result);
        });
    });

    // task fail
    struct FailArgs {
        std::string task;
        std::string line;
        std::string reason;
    };
    auto fail_args = std::make_shared<FailArgs>();
    auto* cmd_fail = group->add_subcommand("fail", "Mark a running task failed");
    add_task_options(cmd_fail, global_opts);
    cmd_fail->add_option("task", fail_args->task, "Task id")->required();
    cmd_fail->add_option("--line", fail_args->line, "Line id (default: session pointer)");
    cmd_fail->add_option("--reason", fail_args->reason, "Failure reason")->required();
    cmd_fail->footer("Example:\n  sele task fail T1 --reason \"tests failed\"");
    cmd_fail->callback([&global_opts, &exit_code, fail_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, fail_args->line);
            ng::require_active(ctx, "task fail");
            require_task(ctx, fail_args->task);

            if (already(ctx, fail_args->task, "failed")) {
                nlohmann::json result;
                result["command"] = "task.fail";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["task"] = fail_args->task;
                result["status"] = "failed";
                output.print_both("Task " + fail_args->task + " is already failed (unchanged)", result);
                return;
            }
            const std::string current = ng::status_of(ctx, fail_args->task);
            if (current != "running") {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + fail_args->task + " is " + current + ", not running",
                     "Only running tasks can fail", {task_line("list")});
            }
            auto& state = ctx.task_state(fail_args->task);
            state.status = "failed";
            state.reason = fail_args->reason;
            state.finished_at = core::sele::utc_now();
            ng::save_runtime(ctx);
            ng::append_event(ctx, "task.fail",
                             {{"task", fail_args->task}, {"reason", fail_args->reason}});

            nlohmann::json result;
            result["command"] = "task.fail";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["task"] = fail_args->task;
            result["status"] = "failed";
            result["reason"] = fail_args->reason;
            output.print_both("Failed " + fail_args->task + ": " + fail_args->reason, result);
        });
    });

    // task retry
    auto retry_args = std::make_shared<TaskArgs>();
    auto* cmd_retry = group->add_subcommand("retry", "Restart a failed task");
    add_task_options(cmd_retry, global_opts);
    cmd_retry->add_option("task", retry_args->task, "Task id")->required();
    cmd_retry->add_option("--line", retry_args->line, "Line id (default: session pointer)");
    cmd_retry->footer("Example:\n  sele task retry T1");
    cmd_retry->callback([&global_opts, &exit_code, retry_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, retry_args->line);
            ng::require_active(ctx, "task retry");
            require_task(ctx, retry_args->task);

            if (already(ctx, retry_args->task, "running")) {
                nlohmann::json result;
                result["command"] = "task.retry";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["task"] = retry_args->task;
                result["status"] = "running";
                output.print_both("Task " + retry_args->task + " is already running (unchanged)", result);
                return;
            }
            const std::string current = ng::status_of(ctx, retry_args->task);
            if (current != "failed") {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + retry_args->task + " is " + current + ", not failed",
                     "Only failed tasks can be retried", {task_line("list")});
            }
            auto& state = ctx.task_state(retry_args->task);
            state.status = "running";
            state.reason.clear();
            state.started_at = core::sele::utc_now();
            ng::save_runtime(ctx);
            ng::append_event(ctx, "task.retry", {{"task", retry_args->task}});

            nlohmann::json result;
            result["command"] = "task.retry";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["task"] = retry_args->task;
            result["status"] = "running";
            output.print_both("Retrying " + retry_args->task, result);
        });
    });

    // task block / unblock
    struct BlockArgs {
        std::string task;
        std::string line;
        std::string reason;
    };
    auto block_args = std::make_shared<BlockArgs>();
    auto* cmd_block = group->add_subcommand("block", "Block a running task on an external event");
    add_task_options(cmd_block, global_opts);
    cmd_block->add_option("task", block_args->task, "Task id")->required();
    cmd_block->add_option("--line", block_args->line, "Line id (default: session pointer)");
    cmd_block->add_option("--reason", block_args->reason, "Blocking reason")->required();
    cmd_block->footer("Example:\n  sele task block T1 --reason \"needs human review\"");
    cmd_block->callback([&global_opts, &exit_code, block_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, block_args->line);
            ng::require_active(ctx, "task block");
            require_task(ctx, block_args->task);

            if (already(ctx, block_args->task, "blocked")) {
                nlohmann::json result;
                result["command"] = "task.block";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["task"] = block_args->task;
                result["status"] = "blocked";
                output.print_both("Task " + block_args->task + " is already blocked (unchanged)", result);
                return;
            }
            const std::string current = ng::status_of(ctx, block_args->task);
            if (current != "running") {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + block_args->task + " is " + current + ", not running",
                     "Only running tasks can be blocked", {task_line("list")});
            }
            auto& state = ctx.task_state(block_args->task);
            state.status = "blocked";
            state.reason = block_args->reason;
            ng::save_runtime(ctx);
            ng::append_event(ctx, "task.block",
                             {{"task", block_args->task}, {"reason", block_args->reason}});

            nlohmann::json result;
            result["command"] = "task.block";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["task"] = block_args->task;
            result["status"] = "blocked";
            result["reason"] = block_args->reason;
            output.print_both("Blocked " + block_args->task + ": " + block_args->reason, result);
        });
    });

    auto unblock_args = std::make_shared<TaskArgs>();
    auto* cmd_unblock = group->add_subcommand("unblock", "Resume a blocked task");
    add_task_options(cmd_unblock, global_opts);
    cmd_unblock->add_option("task", unblock_args->task, "Task id")->required();
    cmd_unblock->add_option("--line", unblock_args->line, "Line id (default: session pointer)");
    cmd_unblock->footer("Example:\n  sele task unblock T1");
    cmd_unblock->callback([&global_opts, &exit_code, unblock_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, unblock_args->line);
            ng::require_active(ctx, "task unblock");
            require_task(ctx, unblock_args->task);

            if (already(ctx, unblock_args->task, "running")) {
                nlohmann::json result;
                result["command"] = "task.unblock";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["task"] = unblock_args->task;
                result["status"] = "running";
                output.print_both("Task " + unblock_args->task + " is already running (unchanged)", result);
                return;
            }
            const std::string current = ng::status_of(ctx, unblock_args->task);
            if (current != "blocked") {
                fail(exit_codes::STATE_CONFLICT,
                     "Task " + unblock_args->task + " is " + current + ", not blocked",
                     "Only blocked tasks can be unblocked", {task_line("list")});
            }
            auto& state = ctx.task_state(unblock_args->task);
            state.status = "running";
            state.reason.clear();
            ng::save_runtime(ctx);
            ng::append_event(ctx, "task.unblock", {{"task", unblock_args->task}});

            nlohmann::json result;
            result["command"] = "task.unblock";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["task"] = unblock_args->task;
            result["status"] = "running";
            output.print_both("Unblocked " + unblock_args->task, result);
        });
    });

    // task revert
    struct TaskRevertArgs {
        std::string task;
        std::string line;
    };
    auto task_revert_args = std::make_shared<TaskRevertArgs>();
    auto* cmd_revert =
        group->add_subcommand("revert", "Undo a task checkpoint and everything after it");
    add_task_options(cmd_revert, global_opts);
    cmd_revert->add_option("task", task_revert_args->task, "Task id")->required();
    cmd_revert->add_option("--line", task_revert_args->line, "Line id (default: session pointer)");
    cmd_revert->footer("Example:\n  sele task revert T2\n"
                       "  sele task revert T1 --line 20261029-fix-login");
    cmd_revert->callback([&global_opts, &exit_code, task_revert_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto root_ctx = ng::load_root(global_opts);
            std::string line_id = task_revert_args->line;
            if (line_id.empty()) {
                line_id = core::session::read(root_ctx.lay, root_ctx.session).line;
            }
            if (line_id.empty()) {
                fail(exit_codes::STATE_CONFLICT,
                     "No active line for session '" + root_ctx.session + "'",
                     "Switch to a line first", {"sele line list"});
            }
            if (!core::sele::valid_line_id(line_id)) {
                fail(exit_codes::USAGE_ERROR, "Invalid line id: " + line_id, "",
                     {"sele line list"});
            }
            if (!core::gitrt::is_repo(root_ctx.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Not a git repository: " + root_ctx.lay.root.string(),
                     "");
            }
            if (core::gitrt::is_dirty(root_ctx.lay.root)) {
                fail(exit_codes::STATE_CONFLICT, "Working tree has uncommitted code changes",
                     "Commit or stash them before reverting", {"git status"});
            }
            const std::string branch = branch_for(line_id);
            if (core::gitrt::branch_exists(root_ctx.lay.root, branch) &&
                core::gitrt::current_branch(root_ctx.lay.root) != branch) {
                core::gitrt::switch_branch(root_ctx.lay.root, branch);
            }

            auto ctx = ng::load(global_opts, line_id);
            if (ctx.line.status == "merged" || ctx.line.status == "abandoned") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + " and cannot be reverted",
                     "Merged or abandoned lines are read-only", {"sele line show " + ctx.line_id});
            }
            require_task(ctx, task_revert_args->task);

            std::size_t index = ctx.tasks.size();
            for (std::size_t i = 0; i < ctx.tasks.size(); ++i) {
                if (ctx.tasks[i].id == task_revert_args->task) {
                    index = i;
                    break;
                }
            }
            if (index == ctx.tasks.size()) {
                fail(exit_codes::STATE_CONFLICT, "Task not found: " + task_revert_args->task,
                     "List tasks for this line", {"sele task list --line " + ctx.line_id});
            }

            const bool root_task = ctx.tasks[index].depends.empty();
            const std::string base_commit = ctx.line.base_commit;
            std::string target;
            std::string anchor;
            if (root_task) {
                target = base_commit;
                anchor = "base";
                if (target.empty()) {
                    fail(exit_codes::STATE_CONFLICT, "Line has no base commit to return to",
                         "Open the line again to record one", {"sele line show " + ctx.line_id});
                }
            } else {
                const std::string previous = ctx.tasks[index].depends.front();
                const auto found = ctx.rt.tasks.find(previous);
                if (found == ctx.rt.tasks.end() || found->second.checkpoint.empty()) {
                    fail(exit_codes::STATE_CONFLICT,
                         "Predecessor " + previous + " has no checkpoint",
                         "Checkpoint the predecessor before reverting " +
                             task_revert_args->task,
                         {"sele task list --line " + ctx.line_id});
                }
                target = found->second.checkpoint;
                anchor = previous;
            }

            const auto reset = core::gitrt::run(ctx.lay.root, {"reset", "--hard", target});
            if (reset.code != 0) {
                fail(exit_codes::ENV_ERROR, "git reset failed: " + reset.out, "");
            }

            auto after = ng::load(global_opts, line_id);
            std::size_t after_index = after.tasks.size();
            for (std::size_t i = 0; i < after.tasks.size(); ++i) {
                if (after.tasks[i].id == task_revert_args->task) {
                    after_index = i;
                    break;
                }
            }
            std::vector<std::string> reset_tasks;
            for (std::size_t i = after_index; i < after.tasks.size(); ++i) {
                after.rt.tasks[after.tasks[i].id] = core::TaskState{};
                reset_tasks.push_back(after.tasks[i].id);
            }
            after.rt.acceptance.clear();
            for (auto it = after.rt.tasks.begin(); it != after.rt.tasks.end();) {
                if (!after.has_task(it->first)) {
                    it = after.rt.tasks.erase(it);
                } else {
                    ++it;
                }
            }
            ng::save_runtime(after);

            if (!root_task && after.line.status == "done") {
                after.line.status = "active";
                after.line.has_closed = false;
                after.line.closed_at.clear();
                ng::save_line(after);
            }
            ng::append_log(after, "task.revert",
                           {{"task", task_revert_args->task}, {"commit", target},
                            {"anchor", anchor}});

            nlohmann::json result;
            result["command"] = "task.revert";
            result["changed"] = true;
            result["line"] = line_id;
            result["task"] = task_revert_args->task;
            result["commit"] = target;
            result["anchor"] = anchor;
            result["root"] = root_task;
            result["reset_tasks"] = reset_tasks;
            result["acceptance_reset"] = true;

            std::ostringstream text;
            text << "Reverted task " << task_revert_args->task << " in " << line_id << " ("
                 << target << ")";
            if (root_task) {
                text << "\n  line: back to draft; reopen with sele line open " << line_id;
            }
            if (!reset_tasks.empty()) {
                text << "\n  reset to pending: " << ng::join(reset_tasks, ", ");
            }
            text << "\n  acceptance: all pending";
            output.print_both(text.str(), result);
        });
    });

    (void)task_line;
}

} // namespace cli
