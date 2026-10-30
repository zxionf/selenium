#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cli/nextgen.hpp"
#include "core/fs.hpp"
#include "core/gitrt.hpp"
#include "core/journal.hpp"
#include "core/model.hpp"
#include "core/sele.hpp"
#include "core/session.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

namespace {

using ng::fail;

std::vector<fs::path> line_directories(const core::sele::Layout& layout) {
    std::vector<fs::path> out;
    if (!core::fsys::exists(layout.lines)) return out;
    for (const auto& entry : fs::directory_iterator(layout.lines)) {
        if (entry.is_directory()) out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

struct LogArgs {
    std::string line;
    int limit = 20;
    bool events = false;
};

void register_log(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto args = std::make_shared<LogArgs>();
    auto* cmd = app.add_subcommand("log", "Show milestone events from .sele/log.jsonl");
    add_global_opts(cmd, global_opts);
    cmd->add_option("--line", args->line, "Filter by line id");
    cmd->add_option("--limit", args->limit, "Show the last N events (default 20)");
    cmd->add_flag("--events", args->events,
                  "Also include runtime/events.jsonl (task/acceptance process events)");
    cmd->footer("Example:\n  sele log\n  sele log --events\n  sele log --line 20261029-fix-login --limit 5");
    cmd->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            const auto log_result = core::journal::read(ctx.lay.log);
            std::vector<nlohmann::ordered_json> all;
            for (auto event : log_result.events) {
                event["source"] = "log";
                all.push_back(event);
            }
            std::size_t bad_lines = log_result.bad_lines;
            if (args->events) {
                const auto event_result = core::journal::read(ctx.lay.events);
                for (auto event : event_result.events) {
                    event["source"] = "events";
                    all.push_back(event);
                }
                bad_lines += event_result.bad_lines;
            }
            std::stable_sort(all.begin(), all.end(), [](const auto& left, const auto& right) {
                return left.value("ts", std::string()) < right.value("ts", std::string());
            });

            std::vector<nlohmann::ordered_json> selected;
            for (const auto& event : all) {
                if (!args->line.empty()) {
                    const std::string line = event.value("line", std::string());
                    if (line != args->line) continue;
                }
                selected.push_back(event);
            }
            if (args->limit > 0 && selected.size() > static_cast<std::size_t>(args->limit)) {
                selected.erase(selected.begin(),
                               selected.end() - static_cast<std::ptrdiff_t>(args->limit));
            }

            nlohmann::json items = nlohmann::json::array();
            std::ostringstream text;
            for (const auto& event : selected) {
                items.push_back(nlohmann::json(event));
                text << event.value("ts", std::string()) << "  "
                     << event.value("actor", std::string()) << "  "
                     << event.value("op", std::string()) << "  "
                     << event.value("line", std::string());
                if (event.contains("task")) text << '/' << event["task"].get<std::string>();
                text << '\n';
            }
            if (selected.empty()) text << "(no events)";

            nlohmann::json result_json;
            result_json["command"] = "log";
            result_json["items"] = items;
            if (bad_lines > 0) result_json["bad_lines"] = bad_lines;
            output.print_both(text.str(), result_json);
        });
    });
}

void register_overview_cmd(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct OverviewArgs {
        std::string epic;
    };
    auto args = std::make_shared<OverviewArgs>();
    auto* cmd = app.add_subcommand("overview", "Show all lines, the session pointer and metadata state");
    add_global_opts(cmd, global_opts);
    cmd->add_option("--epic", args->epic, "Filter by epic id");
    cmd->footer("Example:\n  sele overview\n  sele overview --epic auth-overhaul\n  sele overview --output json");
    cmd->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            const auto pointer = core::session::read(ctx.lay, ctx.session);

            nlohmann::json items = nlohmann::json::array();
            std::ostringstream text;
            int active = 0;
            int done = 0;
            struct EpicCount {
                int total = 0;
                int active = 0;
                int blocked = 0;
                int done = 0;
                int merged = 0;
                int abandoned = 0;
            };
            std::map<std::string, EpicCount> epic_counts;

            for (const auto& directory : line_directories(ctx.lay)) {
                const auto file = directory / "line.json";
                if (!core::fsys::exists(file)) continue;
                const auto line = core::model::line_from_json(core::sele::read_json(file));
                if (!args->epic.empty() && line.epic != args->epic) continue;
                if (!line.epic.empty()) {
                    auto& count = epic_counts[line.epic];
                    ++count.total;
                    if (line.status == "active") ++count.active;
                    else if (line.status == "blocked") ++count.blocked;
                    else if (line.status == "done") ++count.done;
                    else if (line.status == "merged") ++count.merged;
                    else if (line.status == "abandoned") ++count.abandoned;
                }
                core::RuntimeState runtime;
                const auto state_path = core::sele::state_file(ctx.lay, line.id);
                if (core::fsys::exists(state_path)) {
                    runtime = core::model::runtime_from_json(core::sele::read_json(state_path));
                }
                if (line.status == "active") ++active;
                if (line.status == "done") ++done;

                int tasks_done = 0;
                int tasks_total = 0;
                const auto task_dir = core::sele::tasks_dir(ctx.lay, line.id);
                if (core::fsys::exists(task_dir)) {
                    for (const auto& entry : fs::directory_iterator(task_dir)) {
                        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                        ++tasks_total;
                        const auto spec = core::model::task_from_json(core::sele::read_json(entry.path()));
                        const auto found = runtime.tasks.find(spec.id);
                        if (found != runtime.tasks.end() && found->second.status == "done") ++tasks_done;
                    }
                }
                const auto summary = core::model::summarize(line, runtime);
                const bool current = pointer.line == line.id;

                nlohmann::json item;
                item["id"] = line.id;
                item["status"] = line.status;
                item["goal"] = line.goal;
                item["current"] = current;
                item["epic"] = line.epic.empty() ? nlohmann::json(nullptr)
                                                 : nlohmann::json(line.epic);
                item["tasks"] = nlohmann::json::object({{"done", tasks_done}, {"total", tasks_total}});
                item["acceptance"] = nlohmann::json::object({{"pass", summary.passed},
                                                              {"fail", summary.failed},
                                                              {"pending", summary.pending}});
                items.push_back(item);
                text << (current ? "* " : "  ") << line.status << "  " << line.id << "  "
                     << tasks_done << '/' << tasks_total << " tasks  " << summary.passed << '/'
                     << (summary.passed + summary.failed + summary.pending) << " acceptance  "
                     << line.goal << '\n';
            }
            if (items.empty()) text << "(no lines)\n";

            bool meta_dirty = false;
            if (core::gitrt::is_repo(ctx.lay.root)) {
                const auto status = core::gitrt::run(
                    ctx.lay.root, {"status", "--porcelain", "--", ".sele"});
                meta_dirty = status.code == 0 && !status.out.empty();
            }

            text << "\nSession: " << ctx.session << " -> "
                 << (pointer.line.empty() ? "(none)" : pointer.line);
            if (!pointer.task.empty()) text << '/' << pointer.task;
            text << "\nLines: " << items.size() << " total, " << active << " active, " << done
                 << " done";

            nlohmann::json epics = nlohmann::json::array();
            if (core::fsys::exists(ctx.lay.epics)) {
                for (const auto& entry : fs::directory_iterator(ctx.lay.epics)) {
                    if (!entry.is_directory()) continue;
                    const auto file = entry.path() / "epic.json";
                    if (!core::fsys::exists(file)) continue;
                    const auto epic = core::model::epic_from_json(core::sele::read_json(file));
                    if (!args->epic.empty() && epic.id != args->epic) continue;
                    const auto found = epic_counts.find(epic.id);
                    const EpicCount count = found == epic_counts.end() ? EpicCount{} : found->second;
                    nlohmann::json item;
                    item["id"] = epic.id;
                    item["goal"] = epic.goal;
                    item["status"] = epic.status;
                    item["lines"] = nlohmann::json::object({{"total", count.total},
                                                            {"active", count.active},
                                                            {"blocked", count.blocked},
                                                            {"done", count.done},
                                                            {"merged", count.merged},
                                                            {"abandoned", count.abandoned}});
                    epics.push_back(item);
                }
            }
            if (!epics.empty()) {
                text << "\nEpics:";
                for (const auto& epic : epics) {
                    text << "\n  " << epic["status"].get<std::string>() << "  "
                         << epic["id"].get<std::string>() << "  "
                         << epic["lines"]["total"].get<int>() << " lines  "
                         << epic["goal"].get<std::string>();
                }
            } else {
                text << "\nEpics: 0";
            }
            if (meta_dirty) text << "\nMetadata: uncommitted .sele changes";

            nlohmann::json result;
            result["command"] = "overview";
            result["session"] = ctx.session;
            result["current"] = pointer.line;
            result["items"] = items;
            result["epics"] = epics;
            result["meta_dirty"] = meta_dirty;
            output.print_both(text.str(), result);
        });
    });
}

void register_fsck(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        bool fix = false;
    };
    auto args = std::make_shared<Args>();
    auto* cmd = app.add_subcommand("fsck", "Validate .sele consistency");
    add_global_opts(cmd, global_opts);
    cmd->add_flag("--fix", args->fix, "Rebuild runtime state from log.jsonl and repair sessions");
    cmd->footer("Example:\n  sele fsck\n  sele fsck --fix");
    cmd->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        bool fsck_passed = true;
        const int code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            nlohmann::json checks = nlohmann::json::array();
            bool failed = false;

            auto add = [&](const std::string& name, bool ok, const std::string& detail) {
                checks.push_back({{"name", name}, {"passed", ok}, {"detail", detail}});
                if (!ok) failed = true;
            };

            std::size_t line_count = 0;
            std::string schema_detail;
            bool schema_ok = true;
            for (const auto& directory : line_directories(ctx.lay)) {
                const auto file = directory / "line.json";
                if (!core::fsys::exists(file)) {
                    schema_ok = false;
                    schema_detail = "missing line.json in " + directory.filename().string();
                    break;
                }
                try {
                    const auto line = core::model::line_from_json(core::sele::read_json(file));
                    ++line_count;
                    if (!core::sele::valid_line_id(line.id)) {
                        schema_ok = false;
                        schema_detail = "invalid id: " + line.id;
                        break;
                    }
                } catch (const std::exception& error) {
                    schema_ok = false;
                    schema_detail = error.what();
                    break;
                }
            }
            add("schema", schema_ok, schema_ok ? std::to_string(line_count) + " lines" : schema_detail);

            bool epic_schema_ok = true;
            std::string epic_schema_detail = "no epics";
            if (core::fsys::exists(ctx.lay.epics)) {
                for (const auto& entry : fs::directory_iterator(ctx.lay.epics)) {
                    if (!entry.is_directory()) continue;
                    const auto file = entry.path() / "epic.json";
                    if (!core::fsys::exists(file)) {
                        epic_schema_ok = false;
                        epic_schema_detail = "missing epic.json in " + entry.path().filename().string();
                        break;
                    }
                    try {
                        const auto epic = core::model::epic_from_json(core::sele::read_json(file));
                        if (!core::sele::valid_epic_id(epic.id) || epic.goal.empty() ||
                            (epic.status != "active" && epic.status != "done" &&
                             epic.status != "abandoned")) {
                            epic_schema_ok = false;
                            epic_schema_detail = "invalid epic: " + epic.id;
                            break;
                        }
                        if (epic.status == "done" && epic.closed_at.empty()) {
                            epic_schema_ok = false;
                            epic_schema_detail = "epic " + epic.id + " is done without closed_at";
                            break;
                        }
                        if (epic.status == "abandoned" && !epic.has_abandoned) {
                            epic_schema_ok = false;
                            epic_schema_detail = "epic " + epic.id + " is abandoned without reason";
                            break;
                        }
                        epic_schema_detail = "ok";
                    } catch (const std::exception& error) {
                        epic_schema_ok = false;
                        epic_schema_detail = error.what();
                        break;
                    }
                }
            }
            add("epic_schema", epic_schema_ok, epic_schema_detail);

            // Linear chain + frozen + done consistency, per line.
            std::string chain_detail = "ok";
            std::string frozen_detail = "ok";
            std::string done_detail = "ok";
            bool chain_ok = true;
            bool frozen_ok = true;
            bool done_ok = true;
            std::string fix_detail;

            for (const auto& directory : line_directories(ctx.lay)) {
                const auto file = directory / "line.json";
                if (!core::fsys::exists(file)) continue;
                const auto line = core::model::line_from_json(core::sele::read_json(file));

                std::vector<core::TaskSpec> tasks;
                const auto task_dir = core::sele::tasks_dir(ctx.lay, line.id);
                if (core::fsys::exists(task_dir)) {
                    for (const auto& entry : fs::directory_iterator(task_dir)) {
                        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                        tasks.push_back(core::model::task_from_json(core::sele::read_json(entry.path())));
                    }
                    std::sort(tasks.begin(), tasks.end(),
                              [](const core::TaskSpec& left, const core::TaskSpec& right) {
                                  return left.id < right.id;
                              });
                }
                std::vector<std::string> errors;
                std::vector<std::string> warnings;
                ng::check_graph(tasks, errors, warnings);
                if (!errors.empty()) {
                    chain_ok = false;
                    chain_detail = line.id + ": " + errors.front();
                } else if (!warnings.empty()) {
                    chain_detail = line.id + ": warning: " + warnings.front();
                }

                if (line.has_frozen &&
                    (line.status == "active" || line.status == "blocked" ||
                     line.status == "done" || line.status == "merged")) {
                    if (core::goal_digest(line) != line.goal_sha256 ||
                        core::acceptance_digest(line) != line.acceptance_sha256) {
                        frozen_ok = false;
                        frozen_detail = line.id + ": goal or acceptance changed after open";
                    }
                }

                core::RuntimeState runtime;
                const auto state_path = core::sele::state_file(ctx.lay, line.id);
                if (core::fsys::exists(state_path)) {
                    runtime = core::model::runtime_from_json(core::sele::read_json(state_path));
                }

                if (line.status == "done" || line.status == "merged") {
                    for (const auto& task : tasks) {
                        const auto found = runtime.tasks.find(task.id);
                        if (found == runtime.tasks.end() || found->second.status != "done") {
                            done_ok = false;
                            done_detail = line.id + ": task " + task.id + " is not done";
                        }
                    }
                    const auto summary = core::model::summarize(line, runtime);
                    if (summary.pending > 0 || summary.failed > 0) {
                        done_ok = false;
                        done_detail = line.id + ": acceptance not all passing";
                    }
                }
            }
            add("task_graph", chain_ok, chain_detail);
            add("frozen_acceptance", frozen_ok, frozen_detail);
            add("done_consistency", done_ok, done_detail);

            bool epic_refs_ok = true;
            std::string epic_refs_detail = "ok";
            {
                std::map<std::string, std::string> epic_status;
                if (core::fsys::exists(ctx.lay.epics)) {
                    for (const auto& entry : fs::directory_iterator(ctx.lay.epics)) {
                        if (!entry.is_directory()) continue;
                        const auto file = entry.path() / "epic.json";
                        if (!core::fsys::exists(file)) continue;
                        try {
                            const auto epic = core::model::epic_from_json(core::sele::read_json(file));
                            epic_status[epic.id] = epic.status;
                        } catch (...) {
                        }
                    }
                }
                for (const auto& directory : line_directories(ctx.lay)) {
                    const auto file = directory / "line.json";
                    if (!core::fsys::exists(file)) continue;
                    const auto line = core::model::line_from_json(core::sele::read_json(file));
                    if (line.epic.empty()) continue;
                    const auto found = epic_status.find(line.epic);
                    if (found == epic_status.end()) {
                        epic_refs_ok = false;
                        epic_refs_detail = "line " + line.id + " references missing epic " + line.epic;
                        break;
                    }
                    if (found->second == "done" &&
                        (line.status == "active" || line.status == "blocked")) {
                        epic_refs_ok = false;
                        epic_refs_detail = "epic " + line.epic + " is done but line " + line.id +
                                           " is " + line.status;
                        break;
                    }
                }
            }
            add("epic_consistency", epic_refs_ok, epic_refs_detail);

            const auto log_result = core::journal::read(ctx.lay.log);
            add("log_parse", log_result.bad_lines == 0,
                log_result.bad_lines == 0
                    ? std::to_string(log_result.events.size()) + " events"
                    : "bad line " + std::to_string(log_result.first_bad_line));

            const auto pointer = core::session::read(ctx.lay, ctx.session);
            bool session_ok = true;
            std::string session_detail = "no pointer";
            if (!pointer.line.empty()) {
                const auto path = core::sele::line_file(ctx.lay, pointer.line);
                if (!core::fsys::exists(path)) {
                    session_ok = false;
                    session_detail = "session points to missing line " + pointer.line;
                    if (args->fix) {
                        core::session::write(ctx.lay, ctx.session, core::session::Pointer{});
                        session_detail += " (cleared)";
                        fix_detail = "session cleared";
                        session_ok = true;
                    }
                } else {
                    session_detail = pointer.line;
                }
            }
            add("session", session_ok, session_detail);

            if (args->fix) {
                int rebuilt = 0;
                for (const auto& directory : line_directories(ctx.lay)) {
                    const auto file = directory / "line.json";
                    if (!core::fsys::exists(file)) continue;
                    const auto line = core::model::line_from_json(core::sele::read_json(file));
                    auto inner = ng::load(global_opts, line.id);
                    bool changed = false;
                    for (const auto& event : log_result.events) {
                        const std::string event_line = event.value("line", std::string());
                        if (event_line != line.id) continue;
                        const std::string op = event.value("op", std::string());
                        if (op == "task.done") {
                            const std::string task = event.value("task", std::string());
                            if (task.empty() || !inner.has_task(task)) continue;
                            auto& state = inner.task_state(task);
                            if (state.status != "done") {
                                state.status = "done";
                                state.checkpoint = event.value("commit", std::string());
                                state.finished_at = event.value("ts", std::string());
                                changed = true;
                            } else if (state.checkpoint.empty()) {
                                state.checkpoint = event.value("commit", std::string());
                                changed = true;
                            }
                        } else if (op == "acceptance.pass" || op == "acceptance.fail") {
                            const std::string item = event.value("acceptance", std::string());
                            if (item.empty()) continue;
                            bool exists = false;
                            for (const auto& spec : inner.line.acceptance) {
                                if (spec.id == item) exists = true;
                            }
                            if (!exists) continue;
                            auto& state = inner.acc_state(item);
                            const std::string status = op == "acceptance.pass" ? "pass" : "fail";
                            if (state.status != status) {
                                state.status = status;
                                state.evidence = event.value("evidence", std::string());
                                state.by = event.value("by", std::string());
                                state.at = event.value("ts", std::string());
                                changed = true;
                            }
                        }
                    }
                    if (changed) {
                        ng::save_runtime(inner);
                        ++rebuilt;
                    }
                }
                fix_detail = "rebuilt " + std::to_string(rebuilt) + " runtime file(s)" +
                             (fix_detail.empty() ? "" : "; " + fix_detail);
                add("runtime_rebuild", true, fix_detail);
            }

            std::ostringstream text;
            text << (failed ? "Fsck failed" : "Fsck passed");
            for (const auto& check : checks) {
                text << "\n  [" << (check["passed"].get<bool>() ? "ok" : "fail") << "] "
                     << check["name"].get<std::string>() << " (" << check["detail"].get<std::string>()
                     << ')';
            }

            nlohmann::json result;
            result["command"] = "fsck";
            result["passed"] = !failed;
            result["checks"] = checks;
            if (!fix_detail.empty()) result["fix"] = fix_detail;
            output.print_both(text.str(), result);
            fsck_passed = !failed;
        });
        exit_code =
            (code == exit_codes::SUCCESS && !fsck_passed) ? exit_codes::LINT_FAILED : code;
    });
}

} // namespace

void register_overview(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    register_overview_cmd(app, global_opts, exit_code);
    register_log(app, global_opts, exit_code);
    register_fsck(app, global_opts, exit_code);
}

} // namespace cli
