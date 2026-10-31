#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/config.hpp"
#include "core/entity.hpp"
#include "core/event.hpp"
#include "core/fs.hpp"
#include "core/git.hpp"
#include "core/id.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

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

struct CommitArgs {
    std::string task;
    std::string step;
    std::string message;
    std::string plan;
    std::string line;
    std::vector<std::string> files;
};

struct DiffArgs {
    std::string task;
    bool stat{false};
};

struct LogArgs {
    int limit{20};
};

struct ReconcileArgs {
    std::string sync_to;
};

struct StashArgs {
    std::string task;
    std::string plan;
    std::string line;
    std::string message;
};

struct BranchArgs {
    std::string name;
};

} // namespace

void register_git_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* git = app.add_subcommand("git", "Git proxy commands");
    add_global_opts(git, global);

    auto short_flag = std::make_shared<bool>(false);
    auto* status_cmd = git->add_subcommand("status", "Run git status");
    add_global_opts(status_cmd, global);
    status_cmd->add_flag("--short", *short_flag, "Short format");
    status_cmd->callback([&global, &exit_code, short_flag]() {
        const auto ctx = make_context(global);
        std::vector<std::string> args{"status"};
        if (*short_flag) args.push_back("--short");
        const auto result = core::git::run(ctx->paths.root, args);
        ctx->out.print_both(result.output,
                            {{"command", "git.status"},
                             {"ok", result.code == 0},
                             {"output", result.output}});
        if (result.code != 0) exit_code = exit_codes::ENV_ERROR;
    });

    auto diff_args = std::make_shared<DiffArgs>();
    auto* diff_cmd = git->add_subcommand("diff", "Run git diff");
    add_global_opts(diff_cmd, global);
    diff_cmd->add_option("--task", diff_args->task, "Task id (label only)");
    diff_cmd->add_flag("--stat", diff_args->stat, "Show diff stat");
    diff_cmd->callback([&global, &exit_code, diff_args]() {
        const auto ctx = make_context(global);
        std::vector<std::string> args{"diff"};
        if (diff_args->stat) args.push_back("--stat");
        const auto result = core::git::run(ctx->paths.root, args);
        ctx->out.print_both(result.output,
                            {{"command", "git.diff"},
                             {"ok", result.code == 0},
                             {"output", result.output}});
        if (result.code != 0) exit_code = exit_codes::ENV_ERROR;
    });

    auto log_args = std::make_shared<LogArgs>();
    auto* log_cmd = git->add_subcommand("log", "Run git log");
    add_global_opts(log_cmd, global);
    log_cmd->add_option("--limit", log_args->limit, "Number of commits")->default_val(20);
    log_cmd->callback([&global, &exit_code, log_args]() {
        const auto ctx = make_context(global);
        const auto result =
            core::git::run(ctx->paths.root, {"log", "-n", std::to_string(log_args->limit),
                                             "--oneline"});
        ctx->out.print_both(result.output,
                            {{"command", "git.log"},
                             {"ok", result.code == 0},
                             {"output", result.output}});
        if (result.code != 0) exit_code = exit_codes::ENV_ERROR;
    });

    auto commit_args = std::make_shared<CommitArgs>();
    auto* commit_cmd = git->add_subcommand("commit", "Commit through sele");
    add_global_opts(commit_cmd, global);
    commit_cmd->add_option("--task", commit_args->task, "Task id")->required();
    commit_cmd->add_option("--step", commit_args->step, "Step id")->required();
    commit_cmd->add_option("--message", commit_args->message, "Commit message")->required();
    commit_cmd->add_option("--files", commit_args->files, "Files to add (repeatable)")
        ->delimiter(',');
    commit_cmd->add_option("--plan", commit_args->plan, "Plan id");
    commit_cmd->add_option("--line", commit_args->line, "Line id");
    commit_cmd->callback([&global, &exit_code, commit_args]() {
        const auto ctx = make_context(global);
        if (ctx->drift == 2 || ctx->drift == 3) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR,
                 "Git drift " + ctx->drift_message + "; resolve before committing");
            return;
        }
        core::model::Task task;
        if (!load_task(*ctx, exit_code, commit_args->task, commit_args->plan, commit_args->line,
                       task)) {
            return;
        }
        if (task.status != "in_progress") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Task " + task.id + " is " + task.status + ", expected in_progress");
            return;
        }
        auto* step = find_step(task, commit_args->step);
        if (step == nullptr) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Step not found: " + commit_args->step);
            return;
        }
        if (step->status != "in_progress") {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Step " + step->id + " is " + step->status + ", expected in_progress");
            return;
        }

        const auto config = core::config::load(ctx->paths);
        std::string message = commit_args->message;
        if (config.message_convention == "sele") {
            message = "sele(" + task.id + "/" + step->id + "): " + message;
        }
        if (config.module_scope != "off" && !commit_args->files.empty()) {
            std::vector<std::string> outside;
            for (const auto& file : commit_args->files) {
                bool inside = false;
                for (const auto& module : task.modules) {
                    if (file.find(module) != std::string::npos) {
                        inside = true;
                        break;
                    }
                }
                if (!inside) outside.push_back(file);
            }
            if (!outside.empty()) {
                std::string joined;
                for (const auto& file : outside) joined += (joined.empty() ? "" : ",") + file;
                if (config.module_scope == "block") {
                    fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                         "Files outside task modules: " + joined);
                    return;
                }
                ctx->out.print_text("Warning: files outside task modules: " + joined);
            }
        }

        std::vector<std::string> add{"add"};
        if (commit_args->files.empty()) {
            add.push_back("-A");
        } else {
            for (const auto& file : commit_args->files) add.push_back(file);
        }
        const auto add_result = core::git::run(ctx->paths.root, add);
        if (add_result.code != 0) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, "git add failed: " + add_result.output);
            return;
        }
        const auto commit_result =
            core::git::run(ctx->paths.root, {"commit", "-m", message});
        if (commit_result.code != 0) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR,
                 "git commit failed: " + commit_result.output);
            return;
        }

        try {
            const std::string commit = core::git::head(ctx->paths);
            core::model::Event event;
            event.ts = core::model::now_utc();
            event.actor = "agent";
            event.op = "git.commit";
            event.entity = {{"type", "step"},
                            {"plan", task.plan},
                            {"line", task.line},
                            {"task", task.id},
                            {"step", step->id}};
            event.after = commit;
            event.reason = message;
            core::event::append(ctx->paths, event);
            ctx->out.print_both("Committed " + commit,
                                {{"commit", commit},
                                 {"task", task.id},
                                 {"step", step->id}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto stash_args = std::make_shared<StashArgs>();
    auto* stash_cmd = git->add_subcommand("stash", "Stash changes for a task");
    add_global_opts(stash_cmd, global);
    stash_cmd->add_option("--task", stash_args->task, "Task id");
    stash_cmd->add_option("--plan", stash_args->plan, "Plan id");
    stash_cmd->add_option("--line", stash_args->line, "Line id");
    stash_cmd->add_option("--message", stash_args->message, "Stash message");
    stash_cmd->callback([&global, &exit_code, stash_args, stash_cmd]() {
        if (stash_cmd->got_subcommand("pop")) return;
        const auto ctx = make_context(global);
        if (stash_args->task.empty()) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Pass --task");
            return;
        }
        core::model::Task task;
        if (!load_task(*ctx, exit_code, stash_args->task, stash_args->plan, stash_args->line,
                       task)) {
            return;
        }
        std::string message = "sele:" + task.id;
        if (!stash_args->message.empty()) message += " " + stash_args->message;
        const auto result = core::git::run(
            ctx->paths.root, {"stash", "push", "-m", message, "--", ".", ":(exclude).sele"});
        if (result.code != 0) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, "git stash failed: " + result.output);
            return;
        }
        ctx->out.print_both("Stashed for " + task.id,
                            {{"task", task.id}, {"message", message}});
    });

    auto* stash_pop_cmd = stash_cmd->add_subcommand("pop", "Pop the latest stash");
    add_global_opts(stash_pop_cmd, global);
    stash_pop_cmd->callback([&global, &exit_code]() {
        const auto ctx = make_context(global);
        const auto result = core::git::run(ctx->paths.root, {"stash", "pop"});
        if (result.code != 0) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, "git stash pop failed: " + result.output);
            return;
        }
        ctx->out.print_both("Popped stash", {{"command", "git.stash.pop"}});
    });

    auto* branch_cmd = git->add_subcommand("branch", "Branch operations");
    add_global_opts(branch_cmd, global);
    auto create_branch_args = std::make_shared<BranchArgs>();
    auto* create_branch_cmd = branch_cmd->add_subcommand("create", "Create a branch");
    add_global_opts(create_branch_cmd, global);
    create_branch_cmd->add_option("name", create_branch_args->name, "Branch name")->required();
    create_branch_cmd->callback([&global, &exit_code, create_branch_args]() {
        const auto ctx = make_context(global);
        if (!core::id::valid_name(create_branch_args->name)) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Invalid branch name: " + create_branch_args->name);
            return;
        }
        const auto result = core::git::run(ctx->paths.root, {"branch", create_branch_args->name});
        if (result.code != 0) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, "git branch failed: " + result.output);
            return;
        }
        ctx->out.print_both("Created branch " + create_branch_args->name,
                            {{"branch", create_branch_args->name}});
    });

    auto switch_branch_args = std::make_shared<BranchArgs>();
    auto* switch_branch_cmd = branch_cmd->add_subcommand("switch", "Switch branches");
    add_global_opts(switch_branch_cmd, global);
    switch_branch_cmd->add_option("name", switch_branch_args->name, "Branch name")->required();
    switch_branch_cmd->callback([&global, &exit_code, switch_branch_args]() {
        const auto ctx = make_context(global);
        const auto result = core::git::run(ctx->paths.root, {"switch", switch_branch_args->name});
        if (result.code != 0) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, "git switch failed: " + result.output);
            return;
        }
        ctx->out.print_both("Switched to " + switch_branch_args->name,
                            {{"branch", switch_branch_args->name}});
    });

    auto* health_cmd = git->add_subcommand("health", "Show git health");
    add_global_opts(health_cmd, global);
    health_cmd->callback([&global, &exit_code]() {
        const auto ctx = make_context(global);
        (void)exit_code;
        nlohmann::json git = {{"head", core::git::head(ctx->paths)},
                              {"branch", core::git::branch(ctx->paths)}};
        const auto status = core::git::run(ctx->paths.root, {"status", "--porcelain"});
        git["dirty"] = !status.output.empty();
        ctx->out.print_both(
            "Git " + git["branch"].get<std::string>() + " " +
                git["head"].get<std::string>() +
                (git["dirty"].get<bool>() ? " (dirty)" : " (clean)"),
            {{"command", "git.health"}, {"git", git}});
    });

    auto reconcile_args = std::make_shared<ReconcileArgs>();
    auto* reconcile_cmd = git->add_subcommand("reconcile", "Accept the current git HEAD");
    add_global_opts(reconcile_cmd, global);
    reconcile_cmd->add_option("--sync-to", reconcile_args->sync_to, "Accept this HEAD");
    reconcile_cmd->callback([&global, &exit_code, reconcile_args]() {
        const auto ctx = make_context(global);
        if (reconcile_args->sync_to.empty()) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Pass --sync-to <commit>");
            return;
        }
        const auto sha = reconcile_args->sync_to;
        if (core::git::run(ctx->paths.root, {"cat-file", "-e", sha + "^{commit}"}).code != 0) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR, "Unknown commit: " + sha);
            return;
        }
        try {
            core::fsys::write_atomic(
                ctx->paths.cache / "git-fingerprint.json",
                nlohmann::json({{"head", sha}, {"branch", core::git::branch(ctx->paths)}})
                        .dump(2) +
                    "\n");
            append_event(ctx->paths, "human", "git.reconcile", {{"type", "git"}},
                         ctx->drift_message.empty() ? nlohmann::json(nullptr)
                                                    : nlohmann::json(ctx->drift_message),
                         sha, "sync-to");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Accepted HEAD " + sha,
                            {{"command", "git.reconcile"}, {"head", sha}});
    });
}
