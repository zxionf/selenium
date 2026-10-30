#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cli/nextgen.hpp"
#include "core/fs.hpp"
#include "core/journal.hpp"
#include "core/model.hpp"
#include "core/sele.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

namespace {

using ng::fail;

std::size_t utf8_length(const std::string& value) {
    std::size_t count = 0;
    for (unsigned char ch : value) {
        if ((ch & 0xC0u) != 0x80u) ++count;
    }
    return count;
}

core::Epic load_epic(const core::sele::Layout& layout, const std::string& id) {
    if (!core::sele::valid_epic_id(id)) {
        fail(exit_codes::USAGE_ERROR, "Invalid epic id: " + id,
             "Use [a-z][a-z0-9-]{2,40}", {"sele epic list"});
    }
    const auto path = core::sele::epic_file(layout, id);
    if (!core::fsys::exists(path)) {
        fail(exit_codes::STATE_CONFLICT, "Epic not found: " + id, "List epics",
             {"sele epic list"});
    }
    return core::model::epic_from_json(core::sele::read_json(path));
}

void save_epic(const core::sele::Layout& layout, core::Epic& epic) {
    const auto path = core::sele::epic_file(layout, epic.id);
    if (core::fsys::exists(path)) {
        const auto current = core::model::epic_from_json(core::sele::read_json(path));
        if (current.version != epic.version) {
            fail(exit_codes::STATE_CONFLICT,
                 "epic.json was modified by another process (version " +
                     std::to_string(current.version) + " != " + std::to_string(epic.version) +
                     ")",
                 "Re-read the epic and retry", {"sele epic show " + epic.id});
        }
    }
    epic.version += 1;
    core::sele::write_json(path, core::model::to_json(epic));
}

void append_epic_log(const ng::Ctx& ctx, const std::string& op, const std::string& epic_id,
                     const core::sele::json& extra = core::sele::json::object()) {
    core::sele::json entry = core::sele::json::object();
    entry["ts"] = core::sele::utc_now();
    entry["actor"] = ctx.actor;
    entry["op"] = op;
    entry["epic"] = epic_id;
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        entry[it.key()] = it.value();
    }
    core::journal::append(ctx.lay.log, entry);
}

struct EpicSummary {
    int total = 0;
    int active = 0;
    int blocked = 0;
    int done = 0;
    int merged = 0;
    int abandoned = 0;
    int archived = 0;
};

struct EpicLine {
    std::string id;
    std::string status;
    std::string goal;
    bool archived = false;
};

void scan_epic_lines(const core::sele::Layout& layout, const std::string& epic_id,
                     EpicSummary* summary, std::vector<EpicLine>* lines) {
    const auto scan = [&](const fs::path& base, bool archived) {
        if (!core::fsys::exists(base)) return;
        for (const auto& entry : fs::directory_iterator(base)) {
            if (!entry.is_directory()) continue;
            const auto file = entry.path() / "line.json";
            if (!core::fsys::exists(file)) continue;
            const auto value = core::sele::read_json(file);
            if (value.value("epic", std::string()) != epic_id) continue;
            EpicLine line;
            line.id = value.value("id", entry.path().filename().string());
            line.status = value.value("status", std::string());
            line.goal = value.value("goal", std::string());
            line.archived = archived;
            if (lines != nullptr) lines->push_back(line);
            if (summary == nullptr) continue;
            ++summary->total;
            if (archived) ++summary->archived;
            if (line.status == "active") ++summary->active;
            else if (line.status == "blocked") ++summary->blocked;
            else if (line.status == "done") ++summary->done;
            else if (line.status == "merged") ++summary->merged;
            else if (line.status == "abandoned") ++summary->abandoned;
        }
    };
    scan(layout.lines, false);
    scan(layout.sele / "archive", true);
}

nlohmann::json summary_json(const EpicSummary& summary) {
    nlohmann::json value;
    value["total"] = summary.total;
    value["active"] = summary.active;
    value["blocked"] = summary.blocked;
    value["done"] = summary.done;
    value["merged"] = summary.merged;
    value["abandoned"] = summary.abandoned;
    value["archived"] = summary.archived;
    return value;
}

void require_reason(const std::string& reason, const std::string& action) {
    if (reason.empty()) {
        fail(exit_codes::USAGE_ERROR, action + " requires --reason", "");
    }
}

} // namespace

void register_epic(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto* group = app.add_subcommand("epic", "Manage long-lived epics (goal containers)");
    group->require_subcommand(1);

    // epic new
    struct NewArgs {
        std::string id;
        std::string goal;
    };
    auto new_args = std::make_shared<NewArgs>();
    auto* cmd_new = group->add_subcommand("new", "Create an epic");
    add_global_opts(cmd_new, global_opts);
    cmd_new->add_option("id", new_args->id, "Epic id ([a-z][a-z0-9-]{2,40})")->required();
    cmd_new->add_option("--goal", new_args->goal, "One-line long-term goal")->required();
    cmd_new->footer("Example:\n  sele epic new auth-overhaul --goal \"重构认证体系\"");
    cmd_new->callback([&global_opts, &exit_code, new_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            if (!core::sele::valid_epic_id(new_args->id)) {
                fail(exit_codes::USAGE_ERROR, "Invalid epic id: " + new_args->id,
                     "Use [a-z][a-z0-9-]{2,40}", {"sele epic list"});
            }
            if (new_args->goal.empty() || utf8_length(new_args->goal) > 200) {
                fail(exit_codes::USAGE_ERROR, "Goal must be 1..200 characters", "");
            }
            if (core::fsys::exists(core::sele::epic_file(ctx.lay, new_args->id))) {
                fail(exit_codes::STATE_CONFLICT, "Epic already exists: " + new_args->id,
                     "Choose another id", {"sele epic show " + new_args->id});
            }

            core::Epic epic;
            epic.version = 0;
            epic.id = new_args->id;
            epic.goal = new_args->goal;
            epic.status = "active";
            epic.created_at = core::sele::utc_now();
            epic.created_by = ctx.actor;
            core::fsys::ensure_dir(core::sele::epic_dir(ctx.lay, new_args->id));
            save_epic(ctx.lay, epic);
            append_epic_log(ctx, "epic.new", epic.id, {{"goal", epic.goal}});

            nlohmann::json result;
            result["command"] = "epic.new";
            result["changed"] = true;
            result["epic"] = epic.id;
            result["status"] = epic.status;
            result["path"] = core::sele::epic_file(ctx.lay, epic.id).string();

            std::ostringstream text;
            text << "Created epic " << epic.id << " (active)\n"
                 << "  goal: " << epic.goal << '\n'
                 << "  next: sele line new <slug> --epic " << epic.id;
            output.print_both(text.str(), result);
        });
    });

    // epic list
    struct ListArgs {
        std::string status;
    };
    auto list_args = std::make_shared<ListArgs>();
    auto* cmd_list = group->add_subcommand("list", "List epics with child line counts");
    add_global_opts(cmd_list, global_opts);
    cmd_list->add_option("--status", list_args->status, "Filter: active|done|abandoned");
    cmd_list->footer("Example:\n  sele epic list\n  sele epic list --status active");
    cmd_list->callback([&global_opts, &exit_code, list_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            nlohmann::json items = nlohmann::json::array();
            std::ostringstream text;
            int shown = 0;

            if (core::fsys::exists(ctx.lay.epics)) {
                std::vector<fs::path> directories;
                for (const auto& entry : fs::directory_iterator(ctx.lay.epics)) {
                    if (entry.is_directory()) directories.push_back(entry.path());
                }
                std::sort(directories.begin(), directories.end());
                for (const auto& directory : directories) {
                    const auto file = directory / "epic.json";
                    if (!core::fsys::exists(file)) continue;
                    const auto epic = core::model::epic_from_json(core::sele::read_json(file));
                    if (!list_args->status.empty() && epic.status != list_args->status) continue;
                    EpicSummary summary;
                    scan_epic_lines(ctx.lay, epic.id, &summary, nullptr);

                    items.push_back({{"id", epic.id},
                                     {"goal", epic.goal},
                                     {"status", epic.status},
                                     {"created_at", epic.created_at},
                                     {"closed_at", epic.closed_at},
                                     {"lines", summary_json(summary)}});
                    text << epic.status << "  " << epic.id << "  " << summary.total
                         << " lines (" << summary.active << " active)  " << epic.goal << '\n';
                    ++shown;
                }
            }
            if (shown == 0) text << "(no epics)";

            nlohmann::json result;
            result["command"] = "epic.list";
            result["items"] = items;
            output.print_both(text.str(), result);
        });
    });

    // epic show
    struct ShowArgs {
        std::string id;
    };
    auto show_args = std::make_shared<ShowArgs>();
    auto* cmd_show = group->add_subcommand("show", "Show an epic and its child lines");
    add_global_opts(cmd_show, global_opts);
    cmd_show->add_option("id", show_args->id, "Epic id")->required();
    cmd_show->footer("Example:\n  sele epic show auth-overhaul");
    cmd_show->callback([&global_opts, &exit_code, show_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            const auto epic = load_epic(ctx.lay, show_args->id);
            EpicSummary summary;
            std::vector<EpicLine> lines;
            scan_epic_lines(ctx.lay, epic.id, &summary, &lines);
            std::sort(lines.begin(), lines.end(),
                      [](const EpicLine& left, const EpicLine& right) { return left.id < right.id; });

            nlohmann::json line_items = nlohmann::json::array();
            std::ostringstream text;
            text << "Epic: " << epic.id << " [" << epic.status << "]\n"
                 << "Goal: " << epic.goal << '\n'
                 << "Lines: " << summary.total;

            for (const auto& line : lines) {
                line_items.push_back({{"id", line.id},
                                      {"status", line.status},
                                      {"goal", line.goal},
                                      {"archived", line.archived}});
                text << "\n  [" << line.status << "] " << line.id << "  " << line.goal
                     << (line.archived ? "  (archived)" : "");
            }
            if (lines.empty()) text << "\n  (no lines)";

            text << "\nSummary: " << summary.active << " active, " << summary.blocked
                 << " blocked, " << summary.done << " done, " << summary.merged << " merged, "
                 << summary.abandoned << " abandoned, " << summary.archived << " archived";

            nlohmann::json result;
            result["command"] = "epic.show";
            result["epic"] = {{"id", epic.id},
                              {"goal", epic.goal},
                              {"status", epic.status},
                              {"created_at", epic.created_at},
                              {"closed_at", epic.closed_at}};
            result["lines"] = line_items;
            result["summary"] = summary_json(summary);
            output.print_both(text.str(), result);
        });
    });

    // epic amend
    struct AmendArgs {
        std::string id;
        std::string goal;
        std::string reason;
    };
    auto amend_args = std::make_shared<AmendArgs>();
    auto* cmd_amend = group->add_subcommand("amend", "Change an epic goal with a reason");
    add_global_opts(cmd_amend, global_opts);
    cmd_amend->add_option("id", amend_args->id, "Epic id")->required();
    cmd_amend->add_option("--goal", amend_args->goal, "New goal")->required();
    cmd_amend->add_option("--reason", amend_args->reason, "Why the goal changed")->required();
    cmd_amend->footer("Example:\n  sele epic amend auth-overhaul --goal \"...\" --reason \"scope\"");
    cmd_amend->callback([&global_opts, &exit_code, amend_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            auto epic = load_epic(ctx.lay, amend_args->id);
            if (epic.status != "active") {
                fail(exit_codes::STATE_CONFLICT,
                     "Epic " + epic.id + " is " + epic.status + ", not active",
                     "Reopen it first", {"sele epic reopen " + epic.id + " --reason \"...\""});
            }
            if (amend_args->goal.empty() || utf8_length(amend_args->goal) > 200) {
                fail(exit_codes::USAGE_ERROR, "Goal must be 1..200 characters", "");
            }
            const std::string old_goal = epic.goal;
            epic.goal = amend_args->goal;
            save_epic(ctx.lay, epic);
            append_epic_log(ctx, "epic.amend", epic.id,
                            {{"old_goal", old_goal}, {"new_goal", epic.goal},
                             {"reason", amend_args->reason}});

            nlohmann::json result;
            result["command"] = "epic.amend";
            result["changed"] = true;
            result["epic"] = epic.id;
            result["goal"] = epic.goal;
            result["reason"] = amend_args->reason;
            output.print_both("Amended epic " + epic.id + ": " + amend_args->reason, result);
        });
    });

    // epic close
    struct CloseArgs {
        std::string id;
        bool force = false;
    };
    auto close_args = std::make_shared<CloseArgs>();
    auto* cmd_close = group->add_subcommand("close", "Close an epic when its lines are finished");
    add_global_opts(cmd_close, global_opts);
    cmd_close->add_option("id", close_args->id, "Epic id")->required();
    cmd_close->add_flag("--force", close_args->force, "Close even with active/blocked lines");
    cmd_close->footer("Example:\n  sele epic close auth-overhaul\n  sele epic close auth-overhaul --force");
    cmd_close->callback([&global_opts, &exit_code, close_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            auto epic = load_epic(ctx.lay, close_args->id);
            if (epic.status == "done") {
                nlohmann::json result;
                result["command"] = "epic.close";
                result["changed"] = false;
                result["epic"] = epic.id;
                result["status"] = "done";
                output.print_both("Epic " + epic.id + " is already done (unchanged)", result);
                return;
            }
            if (epic.status != "active") {
                fail(exit_codes::STATE_CONFLICT,
                     "Epic " + epic.id + " is " + epic.status + ", not active",
                     "Reopen it first", {"sele epic show " + epic.id});
            }

            EpicSummary summary;
            scan_epic_lines(ctx.lay, epic.id, &summary, nullptr);
            const int open_lines = summary.active + summary.blocked;
            if (open_lines > 0 && !close_args->force) {
                fail(exit_codes::STATE_CONFLICT,
                     "Epic " + epic.id + " has " + std::to_string(open_lines) +
                         " active/blocked line(s)",
                     "Finish or archive them, or pass --force",
                     {"sele epic show " + epic.id});
            }

            epic.status = "done";
            epic.closed_at = core::sele::utc_now();
            save_epic(ctx.lay, epic);
            append_epic_log(ctx, "epic.close", epic.id, {{"forced", close_args->force}});

            nlohmann::json result;
            result["command"] = "epic.close";
            result["changed"] = true;
            result["epic"] = epic.id;
            result["status"] = "done";
            result["forced"] = close_args->force;
            output.print_both("Closed epic " + epic.id + (close_args->force ? " (forced)" : ""),
                              result);
        });
    });

    // epic reopen
    struct ReopenArgs {
        std::string id;
        std::string reason;
    };
    auto reopen_args = std::make_shared<ReopenArgs>();
    auto* cmd_reopen = group->add_subcommand("reopen", "Reopen a done epic");
    add_global_opts(cmd_reopen, global_opts);
    cmd_reopen->add_option("id", reopen_args->id, "Epic id")->required();
    cmd_reopen->add_option("--reason", reopen_args->reason, "Why it is reopened")->required();
    cmd_reopen->footer("Example:\n  sele epic reopen auth-overhaul --reason \"new scope\"");
    cmd_reopen->callback([&global_opts, &exit_code, reopen_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            auto epic = load_epic(ctx.lay, reopen_args->id);
            require_reason(reopen_args->reason, "epic reopen");
            if (epic.status == "active") {
                nlohmann::json result;
                result["command"] = "epic.reopen";
                result["changed"] = false;
                result["epic"] = epic.id;
                result["status"] = "active";
                output.print_both("Epic " + epic.id + " is already active (unchanged)", result);
                return;
            }
            if (epic.status != "done") {
                fail(exit_codes::STATE_CONFLICT,
                     "Epic " + epic.id + " is " + epic.status + ", not done",
                     "Only done epics can be reopened", {"sele epic show " + epic.id});
            }
            epic.status = "active";
            epic.closed_at.clear();
            save_epic(ctx.lay, epic);
            append_epic_log(ctx, "epic.reopen", epic.id, {{"reason", reopen_args->reason}});

            nlohmann::json result;
            result["command"] = "epic.reopen";
            result["changed"] = true;
            result["epic"] = epic.id;
            result["status"] = "active";
            result["reason"] = reopen_args->reason;
            output.print_both("Reopened epic " + epic.id + ": " + reopen_args->reason, result);
        });
    });

    // epic abandon
    struct AbandonArgs {
        std::string id;
        std::string reason;
    };
    auto abandon_args = std::make_shared<AbandonArgs>();
    auto* cmd_abandon = group->add_subcommand("abandon", "Abandon an active epic");
    add_global_opts(cmd_abandon, global_opts);
    cmd_abandon->add_option("id", abandon_args->id, "Epic id")->required();
    cmd_abandon->add_option("--reason", abandon_args->reason, "Why it is abandoned")->required();
    cmd_abandon->footer("Example:\n  sele epic abandon auth-overhaul --reason \"superseded\"");
    cmd_abandon->callback([&global_opts, &exit_code, abandon_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            auto epic = load_epic(ctx.lay, abandon_args->id);
            require_reason(abandon_args->reason, "epic abandon");
            if (epic.status == "abandoned") {
                nlohmann::json result;
                result["command"] = "epic.abandon";
                result["changed"] = false;
                result["epic"] = epic.id;
                result["status"] = "abandoned";
                output.print_both("Epic " + epic.id + " is already abandoned (unchanged)", result);
                return;
            }
            if (epic.status == "done") {
                fail(exit_codes::STATE_CONFLICT,
                     "Epic " + epic.id + " is done, not abandoned",
                     "Reopen it first", {"sele epic reopen " + epic.id + " --reason \"...\""});
            }
            epic.status = "abandoned";
            epic.has_abandoned = true;
            epic.abandoned_reason = abandon_args->reason;
            epic.abandoned_at = core::sele::utc_now();
            save_epic(ctx.lay, epic);
            append_epic_log(ctx, "epic.abandon", epic.id, {{"reason", abandon_args->reason}});

            nlohmann::json result;
            result["command"] = "epic.abandon";
            result["changed"] = true;
            result["epic"] = epic.id;
            result["status"] = "abandoned";
            result["reason"] = abandon_args->reason;
            output.print_both("Abandoned epic " + epic.id + ": " + abandon_args->reason, result);
        });
    });
}

} // namespace cli
