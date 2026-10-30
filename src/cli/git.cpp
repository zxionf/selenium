#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/checkpoint.hpp"
#include "core/entity.hpp"
#include "core/fs.hpp"
#include "core/git.hpp"
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
    bool check{false};
    std::string sync_to;
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
    auto* commit_cmd = git->add_subcommand("commit", "Commit through sele and create a checkpoint");
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
            core::git::run(ctx->paths.root, {"commit", "-m", commit_args->message});
        if (commit_result.code != 0) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR,
                 "git commit failed: " + commit_result.output);
            return;
        }

        try {
            const auto checkpoint = core::checkpoint::create(ctx->paths, task, *step);
            step->checkpoint = checkpoint.id;
            core::store::save_task(ctx->paths, task);
            append_event(ctx->paths, "agent", "git.commit", checkpoint.entity, nullptr,
                         checkpoint.id, commit_args->message);
            ctx->out.print_both(
                "Committed " + checkpoint.id,
                {{"checkpoint", checkpoint.id},
                 {"commit", checkpoint.git.value("commit", "")},
                 {"task", task.id},
                 {"step", step->id}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto reconcile_args = std::make_shared<ReconcileArgs>();
    auto* reconcile_cmd = git->add_subcommand("reconcile", "Check or accept git drift");
    add_global_opts(reconcile_cmd, global);
    reconcile_cmd->add_flag("--check", reconcile_args->check,
                            "Revalidate checkpoints (D4/D5)");
    reconcile_cmd->add_option("--sync-to", reconcile_args->sync_to, "Accept this HEAD");
    reconcile_cmd->callback([&global, &exit_code, reconcile_args]() {
        const auto ctx = make_context(global);
        if (!reconcile_args->sync_to.empty()) {
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
            return;
        }

        int valid = 0;
        int stale = 0;
        int orphaned = 0;
        int invalidated = 0;
        try {
            for (auto& checkpoint : load_checkpoints(ctx->paths)) {
                if (checkpoint.status == "invalidated") {
                    ++invalidated;
                    continue;
                }
                const std::string commit = checkpoint.git.value("commit", "");
                if (commit.empty() || core::git::run(ctx->paths.root,
                                                     {"cat-file", "-e",
                                                      commit + "^{commit}"})
                                             .code != 0) {
                    checkpoint.status = "orphaned";
                    core::store::save_checkpoint(ctx->paths, checkpoint);
                    ++orphaned;
                    continue;
                }
                const auto tree =
                    core::git::run(ctx->paths.root, {"rev-parse", commit + "^{tree}"});
                const std::string expected = checkpoint.git.value("tree", "");
                if (!expected.empty() && tree.code == 0 && tree.output != expected) {
                    checkpoint.status = "stale";
                    core::store::save_checkpoint(ctx->paths, checkpoint);
                    ++stale;
                    continue;
                }
                ++valid;
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        nlohmann::json result = {{"command", "git.reconcile"},
                                 {"valid", valid},
                                 {"stale", stale},
                                 {"orphaned", orphaned},
                                 {"invalidated", invalidated},
                                 {"drift", ctx->drift_message}};
        ctx->out.print_both("reconcile: valid=" + std::to_string(valid) +
                                " stale=" + std::to_string(stale) +
                                " orphaned=" + std::to_string(orphaned) +
                                " invalidated=" + std::to_string(invalidated),
                            result);
    });
}
