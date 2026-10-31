#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/fs.hpp"
#include "core/id.hpp"
#include "core/state.hpp"
#include "core/store.hpp"
#include "util/color.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

nlohmann::json plan_entity(const core::model::Plan& plan) {
    return {{"type", "plan"}, {"plan", plan.id}};
}

std::string archive_stamp() {
    const auto now = core::model::now_utc();
    std::string stamp;
    for (const char character : now) {
        if (character >= '0' && character <= '9') stamp += character;
    }
    return stamp;
}

std::vector<std::filesystem::path> collect_plan_files(const core::store::Paths& paths,
                                                      const std::string& plan_id) {
    std::vector<std::filesystem::path> files;
    files.push_back(core::store::plan_path(paths, plan_id));

    if (core::fsys::exists(paths.lines)) {
        for (const auto& entry : std::filesystem::directory_iterator(paths.lines)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            if (entry.path().filename().string().rfind(plan_id + ".", 0) != 0) continue;
            files.push_back(entry.path());
        }
    }
    if (core::fsys::exists(paths.tasks)) {
        for (const auto& bucket : std::filesystem::directory_iterator(paths.tasks)) {
            if (!bucket.is_directory()) continue;
            for (const auto& entry : std::filesystem::directory_iterator(bucket.path())) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                try {
                    const auto task = core::model::task_from_json(
                        nlohmann::json::parse(core::fsys::read_file(entry.path())));
                    if (task.plan == plan_id) files.push_back(entry.path());
                } catch (...) {
                }
            }
        }
    }
    if (core::fsys::exists(paths.checkpoints)) {
        for (const auto& month : std::filesystem::directory_iterator(paths.checkpoints)) {
            if (!month.is_directory()) continue;
            for (const auto& entry : std::filesystem::directory_iterator(month.path())) {
                if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                try {
                    const auto checkpoint = core::model::checkpoint_from_json(
                        nlohmann::json::parse(core::fsys::read_file(entry.path())));
                    if (checkpoint.entity.value("plan", "") == plan_id) files.push_back(entry.path());
                } catch (...) {
                }
            }
        }
    }
    return files;
}

std::filesystem::path latest_archive(const core::store::Paths& paths,
                                     const std::string& plan_id) {
    std::filesystem::path best;
    if (!core::fsys::exists(paths.archive)) return best;
    for (const auto& entry : std::filesystem::directory_iterator(paths.archive)) {
        if (!entry.is_directory()) continue;
        const auto name = entry.path().filename().string();
        if (name.rfind(plan_id + "-", 0) != 0) continue;
        if (best.empty() || name > best.filename().string()) best = entry.path();
    }
    return best;
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

bool transition_plan(Context& ctx, int& exit_code, core::model::Plan& plan,
                     const std::string& to) {
    if (!core::state::plan_transition(plan.status, to)) {
        fail(ctx, exit_code, exit_codes::STATE_CONFLICT,
             "Plan " + plan.id + " cannot go from " + plan.status + " to " + to);
        return false;
    }
    return true;
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

struct SwitchArgs {
    std::string id;
};

struct SubmitArgs {
    std::string id;
    std::string reason{"submit for approval"};
};

struct ApproveArgs {
    std::string id;
    std::string by{"human"};
    std::string reason{"approve plan"};
};

struct RejectArgs {
    std::string id;
    std::string reason;
};

struct DoneArgs {
    std::string id;
    std::string reason{"all lines done"};
};

struct StatusArgs {
    std::string id;
};

struct ArchiveArgs {
    std::string id;
    std::string reason{"archive plan"};
};

struct AbortArgs {
    std::string id;
    std::string reason{"abort plan"};
};

struct RestoreArgs {
    std::string id;
    std::string from;
    std::string reason{"restore plan"};
};

struct VerifyArgs {
    std::string id;
    std::string acceptance;
    std::string by{"human"};
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
            std::ostringstream text;
            text << "Plans: " << items.size();
            for (const auto& item : items) {
                text << "\n  " << util::color::green(item["status"].get<std::string>())
                     << "  " << item["id"].get<std::string>() << "  "
                     << item["goal"].get<std::string>() << "  (" << item["lines"].get<int>()
                     << " lines)";
            }
            ctx->out.print_both(text.str(), {{"command", "plan.list"}, {"items", items}});
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
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, show_args->id, item)) return;
        ctx->out.print_both("Plan " + item.id + " [" + item.status + "] " + item.goal,
                            core::model::to_json(item));
    });

    auto switch_args = std::make_shared<SwitchArgs>();
    auto* switch_cmd = plan->add_subcommand("switch", "Set the current plan context");
    add_global_opts(switch_cmd, global);
    switch_cmd->add_option("id", switch_args->id, "Plan id")->required();
    switch_cmd->callback([&global, &exit_code, switch_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, switch_args->id, item)) return;
        nlohmann::json context = nlohmann::json::object();
        if (core::fsys::exists(ctx->paths.context)) {
            try {
                context = nlohmann::json::parse(core::fsys::read_file(ctx->paths.context));
            } catch (...) {
                context = nlohmann::json::object();
            }
        }
        context["plan"] = item.id;
        context.erase("line");
        try {
            core::fsys::write_atomic(ctx->paths.context, context.dump(2) + "\n");
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Switched to plan " + item.id, {{"plan", item.id}});
    });

    auto submit_args = std::make_shared<SubmitArgs>();
    auto* submit_cmd = plan->add_subcommand("submit", "Submit a draft plan for approval");
    add_global_opts(submit_cmd, global);
    submit_cmd->add_option("id", submit_args->id, "Plan id")->required();
    submit_cmd->add_option("--reason", submit_args->reason, "Decision reason");
    submit_cmd->callback([&global, &exit_code, submit_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, submit_args->id, item)) return;
        if (item.lines.empty()) {
            fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                 "Plan must declare at least one line before submit");
            return;
        }
        if (!transition_plan(*ctx, exit_code, item, "pending_approval")) return;
        item.status = "pending_approval";
        try {
            core::store::save_plan(ctx->paths, item);
            append_event(ctx->paths, "agent", "plan.submit", plan_entity(item), "draft",
                         item.status, submit_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Submitted plan " + item.id,
                            {{"plan", item.id}, {"status", item.status}});
    });

    auto approve_args = std::make_shared<ApproveArgs>();
    auto* approve_cmd = plan->add_subcommand("approve", "Approve a pending plan");
    add_global_opts(approve_cmd, global);
    approve_cmd->add_option("id", approve_args->id, "Plan id")->required();
    approve_cmd->add_option("--by", approve_args->by, "Approver identity")->default_val("human");
    approve_cmd->add_option("--reason", approve_args->reason, "Decision reason");
    approve_cmd->callback([&global, &exit_code, approve_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, approve_args->id, item)) return;
        if (!transition_plan(*ctx, exit_code, item, "active")) return;
        item.status = "active";
        item.approved_by = approve_args->by;
        item.approved_at = core::model::now_utc();
        try {
            core::store::save_plan(ctx->paths, item);
            append_event(ctx->paths, approve_args->by, "plan.approve", plan_entity(item),
                         "pending_approval", item.status, approve_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Approved plan " + item.id,
                            {{"plan", item.id}, {"status", item.status}});
    });

    auto reject_args = std::make_shared<RejectArgs>();
    auto* reject_cmd = plan->add_subcommand("reject", "Reject a pending plan");
    add_global_opts(reject_cmd, global);
    reject_cmd->add_option("id", reject_args->id, "Plan id")->required();
    reject_cmd->add_option("--reason", reject_args->reason, "Rejection reason")->required();
    reject_cmd->callback([&global, &exit_code, reject_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, reject_args->id, item)) return;
        if (!transition_plan(*ctx, exit_code, item, "draft")) return;
        item.status = "draft";
        try {
            core::store::save_plan(ctx->paths, item);
            append_event(ctx->paths, "human", "plan.reject", plan_entity(item),
                         "pending_approval", item.status, reject_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Rejected plan " + item.id,
                            {{"plan", item.id}, {"status", item.status}});
    });

    auto done_args = std::make_shared<DoneArgs>();
    auto* done_cmd = plan->add_subcommand("done", "Mark an active plan done");
    add_global_opts(done_cmd, global);
    done_cmd->add_option("id", done_args->id, "Plan id")->required();
    done_cmd->add_option("--reason", done_args->reason, "Decision reason");
    done_cmd->callback([&global, &exit_code, done_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, done_args->id, item)) return;
        if (!transition_plan(*ctx, exit_code, item, "done")) return;

        try {
            for (const auto& ref : item.lines) {
                const auto path = core::store::line_path(ctx->paths, item.id, ref.id);
                if (!core::fsys::exists(path)) {
                    fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                         "Line not found: " + ref.id);
                    return;
                }
                const auto line =
                    core::model::line_from_json(nlohmann::json::parse(core::fsys::read_file(path)));
                if (line.status != "done" && line.status != "abandoned") {
                    fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                         "Line " + ref.id + " is still " + line.status);
                    return;
                }
            }
            for (const auto& acceptance : item.acceptance) {
                if (acceptance.status != "pass") {
                    fail(*ctx, exit_code, exit_codes::STATE_CONFLICT,
                         "Acceptance " + acceptance.id + " is " + acceptance.status);
                    return;
                }
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }

        item.status = "done";
        try {
            core::store::save_plan(ctx->paths, item);
            append_event(ctx->paths, "human", "plan.done", plan_entity(item), "active",
                         item.status, done_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Closed plan " + item.id,
                            {{"plan", item.id}, {"status", item.status}});
    });

    auto status_args = std::make_shared<StatusArgs>();
    auto* status_cmd = plan->add_subcommand("status", "Show plan status and line counts");
    add_global_opts(status_cmd, global);
    status_cmd->add_option("id", status_args->id, "Plan id")->required();
    status_cmd->callback([&global, &exit_code, status_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, status_args->id, item)) return;
        nlohmann::json counts = nlohmann::json::object();
        try {
            for (const auto& line : core::store::load_lines(ctx->paths)) {
                if (line.plan != item.id) continue;
                counts[line.status] = counts.value(line.status, 0) + 1;
            }
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Plan " + item.id + " [" + item.status + "]",
                            {{"plan", item.id},
                             {"status", item.status},
                             {"lines", counts},
                             {"acceptance", item.acceptance.size()}});
    });

    auto abort_args = std::make_shared<AbortArgs>();
    auto* abort_cmd = plan->add_subcommand("abort", "Abort a plan");
    add_global_opts(abort_cmd, global);
    abort_cmd->add_option("id", abort_args->id, "Plan id")->required();
    abort_cmd->add_option("--reason", abort_args->reason, "Abort reason");
    abort_cmd->callback([&global, &exit_code, abort_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, abort_args->id, item)) return;
        if (!transition_plan(*ctx, exit_code, item, "aborted")) return;
        item.status = "aborted";
        try {
            core::store::save_plan(ctx->paths, item);
            append_event(ctx->paths, "human", "plan.abort", plan_entity(item), item.status,
                         "aborted", abort_args->reason);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Aborted plan " + item.id,
                            {{"plan", item.id}, {"status", item.status}});
    });

    auto archive_args = std::make_shared<ArchiveArgs>();
    auto* archive_cmd = plan->add_subcommand("archive", "Move a done plan to archived");
    add_global_opts(archive_cmd, global);
    archive_cmd->add_option("id", archive_args->id, "Plan id")->required();
    archive_cmd->add_option("--reason", archive_args->reason, "Decision reason");
    archive_cmd->callback([&global, &exit_code, archive_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, archive_args->id, item)) return;
        if (!transition_plan(*ctx, exit_code, item, "archived")) return;
        item.status = "archived";
        try {
            core::store::save_plan(ctx->paths, item);
            const auto files = collect_plan_files(ctx->paths, item.id);
            const auto target = ctx->paths.archive / (item.id + "-" + archive_stamp());
            core::fsys::ensure_dir(target);
            nlohmann::json manifest = {{"plan", item.id},
                                       {"ts", archive_stamp()},
                                       {"files", nlohmann::json::array()}};
            for (const auto& file : files) {
                if (!core::fsys::exists(file)) continue;
                const auto relative = std::filesystem::relative(file, ctx->paths.sele);
                const auto destination = target / relative;
                core::fsys::ensure_dir(destination.parent_path());
                std::filesystem::rename(file, destination);
                manifest["files"].push_back({{"path", relative.generic_string()}});
            }
            core::fsys::write_atomic(target / "_manifest.json", manifest.dump(2) + "\n");
            append_event(ctx->paths, "human", "plan.archive", plan_entity(item), "done",
                         item.status, archive_args->reason);
            ctx->out.print_both("Archived plan " + item.id + " to " + target.string(),
                                {{"plan", item.id},
                                 {"status", item.status},
                                 {"archive", target.string()},
                                 {"files", manifest["files"].size()}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto restore_args = std::make_shared<RestoreArgs>();
    auto* restore_cmd = plan->add_subcommand("restore", "Restore an archived plan to active");
    add_global_opts(restore_cmd, global);
    restore_cmd->add_option("id", restore_args->id, "Plan id")->required();
    restore_cmd->add_option("--from", restore_args->from, "Archive timestamp");
    restore_cmd->add_option("--reason", restore_args->reason, "Decision reason");
    restore_cmd->callback([&global, &exit_code, restore_args]() {
        const auto ctx = make_context(global);
        std::filesystem::path source = restore_args->from.empty()
                                           ? latest_archive(ctx->paths, restore_args->id)
                                           : ctx->paths.archive /
                                                 (restore_args->id + "-" + restore_args->from);
        if (source.empty() || !core::fsys::exists(source)) {
            fail(*ctx, exit_code, exit_codes::USAGE_ERROR,
                 "Archive not found for plan: " + restore_args->id);
            return;
        }
        try {
            const auto manifest_path = source / "_manifest.json";
            if (!core::fsys::exists(manifest_path)) {
                fail(*ctx, exit_code, exit_codes::ENV_ERROR,
                     "Missing manifest in " + source.string());
                return;
            }
            const auto manifest = nlohmann::json::parse(core::fsys::read_file(manifest_path));
            std::size_t restored = 0;
            for (const auto& entry : manifest.value("files", nlohmann::json::array())) {
                const auto relative = entry.value("path", "");
                if (relative.empty()) continue;
                const auto from = source / relative;
                const auto to = ctx->paths.sele / relative;
                if (!core::fsys::exists(from)) continue;
                core::fsys::ensure_dir(to.parent_path());
                std::filesystem::rename(from, to);
                ++restored;
            }
            const auto plan_file = core::store::plan_path(ctx->paths, restore_args->id);
            if (core::fsys::exists(plan_file)) {
                auto plan = core::model::plan_from_json(
                    nlohmann::json::parse(core::fsys::read_file(plan_file)));
                plan.status = "active";
                core::store::save_plan(ctx->paths, plan);
                append_event(ctx->paths, "human", "plan.restore", plan_entity(plan),
                             "archived", plan.status, restore_args->reason);
            }
            ctx->out.print_both("Restored plan " + restore_args->id,
                                {{"plan", restore_args->id},
                                 {"status", "active"},
                                 {"files", restored}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });

    auto verify_args = std::make_shared<VerifyArgs>();
    auto* verify_cmd = plan->add_subcommand("verify", "Verify a plan acceptance point");
    add_global_opts(verify_cmd, global);
    verify_cmd->add_option("id", verify_args->id, "Plan id")->required();
    verify_cmd->add_option("acceptance", verify_args->acceptance, "Acceptance id")->required();
    verify_cmd->add_option("--by", verify_args->by, "Verifier identity")->default_val("human");
    verify_cmd->callback([&global, &exit_code, verify_args]() {
        const auto ctx = make_context(global);
        core::model::Plan item;
        if (!load_plan(*ctx, exit_code, verify_args->id, item)) return;
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
            core::store::save_plan(ctx->paths, item);
            append_event(ctx->paths, verify_args->by, "plan.verify", plan_entity(item),
                         "unverified", "pass", verify_args->acceptance);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }
        ctx->out.print_both("Verified " + verify_args->acceptance + " on plan " + item.id,
                            {{"plan", item.id},
                             {"acceptance", verify_args->acceptance},
                             {"status", "pass"}});
    });
}
