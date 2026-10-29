#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
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

namespace fs = std::filesystem;

namespace cli {

namespace {

using ng::fail;
using ng::json;

const std::vector<std::string> kTypes = {"feature", "fix", "chore", "refactor", "spike"};

bool valid_module(const std::string& module) {
    static const std::regex pattern("^[a-z][a-z0-9-]*$");
    return std::regex_match(module, pattern);
}

std::string comma_list(const std::vector<std::string>& values) {
    return ng::join(values, ", ");
}

std::string branch_for(const std::string& line_id) {
    return "sele/" + line_id;
}

void add_line_options(CLI::App* sub, GlobalOptions& global_opts) {
    add_global_opts(sub, global_opts);
}

nlohmann::json line_view(const core::Line& line) {
    nlohmann::json value;
    value["id"] = line.id;
    value["type"] = line.type;
    value["goal"] = line.goal;
    value["modules"] = line.modules;
    value["status"] = line.status;
    value["version"] = line.version;
    value["acceptance"] = nlohmann::json::array();
    for (const auto& item : line.acceptance) {
        value["acceptance"].push_back({{"id", item.id}, {"desc", item.desc}, {"check", item.check}});
    }
    return value;
}

std::string trim_text(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    return value.substr(begin, end - begin);
}

} // namespace

void register_line(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto* group = app.add_subcommand("line", "Manage lines (goal + acceptance + tasks)");
    group->require_subcommand(1);

    // line new
    struct NewArgs {
        std::string slug;
        std::string goal;
        std::string modules;
        std::string type = "feature";
        std::string id;
        std::vector<std::string> acceptance;
    };
    auto new_args = std::make_shared<NewArgs>();
    auto* cmd_new = group->add_subcommand("new", "Create a line in draft state");
    add_line_options(cmd_new, global_opts);
    cmd_new->add_option("slug", new_args->slug, "Slug (lowercase letters, digits, hyphens)")->required();
    cmd_new->add_option("--goal", new_args->goal, "One-sentence goal")->required();
    cmd_new->add_option("--acceptance", new_args->acceptance,
                        "Acceptance item as <desc>::<check> (repeatable)")->required();
    cmd_new->add_option("--modules", new_args->modules, "Comma-separated module tags")->required();
    cmd_new->add_option("--type", new_args->type, "feature|fix|chore|refactor|spike");
    cmd_new->add_option("--id", new_args->id, "Full line id (default: <UTC date>-<slug>)");
    cmd_new->footer("Example:\n  sele line new fix-login --goal \"Login shows actionable errors\" "
                    "--acceptance \"wrong password returns 401::pytest tests/test_login.py\" --modules auth");
    cmd_new->callback([&global_opts, &exit_code, new_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            if (!core::sele::valid_slug(new_args->slug)) {
                fail(exit_codes::USAGE_ERROR,
                     "Invalid slug: must start with a lowercase letter or digit and use [a-z0-9-]",
                     "sele line new fix-login --goal ... --modules auth");
            }
            if (new_args->goal.empty() || new_args->goal.size() > 120) {
                fail(exit_codes::USAGE_ERROR, "Goal must be 1..120 characters", "");
            }
            if (std::find(kTypes.begin(), kTypes.end(), new_args->type) == kTypes.end()) {
                fail(exit_codes::USAGE_ERROR,
                     "Invalid --type: " + new_args->type, "Use feature, fix, chore, refactor, or spike");
            }

            const auto modules = ng::split_csv(new_args->modules);
            if (modules.empty()) {
                fail(exit_codes::USAGE_ERROR, "--modules requires at least one tag", "");
            }
            for (const auto& module : modules) {
                if (!valid_module(module)) {
                    fail(exit_codes::USAGE_ERROR, "Invalid module tag: " + module,
                         "Module tags use [a-z][a-z0-9-]*");
                }
            }
            if (!ctx.conf.modules_allow.empty()) {
                for (const auto& module : modules) {
                    if (std::find(ctx.conf.modules_allow.begin(), ctx.conf.modules_allow.end(),
                                  module) == ctx.conf.modules_allow.end()) {
                        fail(exit_codes::USAGE_ERROR, "Module not registered: " + module,
                             "Add it to .sele/config.json modules.allow or remove the check");
                    }
                }
            }

            std::vector<core::Acceptance> acceptance;
            for (std::size_t i = 0; i < new_args->acceptance.size(); ++i) {
                const std::string& raw = new_args->acceptance[i];
                const auto split = raw.find("::");
                if (split == std::string::npos) {
                    fail(exit_codes::USAGE_ERROR,
                         "Invalid --acceptance (missing ::): " + raw,
                         "Format: \"<desc>::<check>\"");
                }
                const std::string desc = raw.substr(0, split);
                const std::string check = raw.substr(split + 2);
                if (desc.empty() || check.empty()) {
                    fail(exit_codes::USAGE_ERROR, "Empty acceptance desc or check: " + raw, "");
                }
                acceptance.push_back(
                    core::Acceptance{"A" + std::to_string(i + 1), desc, check});
            }

            const std::string id =
                new_args->id.empty() ? core::sele::utc_stamp() + "-" + new_args->slug : new_args->id;
            if (!core::sele::valid_line_id(id)) {
                fail(exit_codes::USAGE_ERROR, "Invalid line id: " + id,
                     "Use <YYYYMMDD>-<slug>");
            }
            if (core::fsys::exists(core::sele::line_file(ctx.lay, id))) {
                fail(exit_codes::STATE_CONFLICT, "Line already exists: " + id, "Choose another slug",
                     {"sele line list"});
            }

            ctx.line_id = id;
            ctx.line = core::Line{};
            ctx.line.version = 0;
            ctx.line.id = id;
            ctx.line.type = new_args->type;
            ctx.line.goal = new_args->goal;
            ctx.line.acceptance = acceptance;
            ctx.line.modules = modules;
            ctx.line.created_at = core::sele::utc_now();
            ctx.line.created_by = ctx.actor;
            ctx.line.status = "draft";

            core::fsys::ensure_dir(core::sele::tasks_dir(ctx.lay, id));
            ng::save_line(ctx);
            ng::append_log(ctx, "line.new");

            core::session::Pointer pointer;
            pointer.line = id;
            core::session::write(ctx.lay, ctx.session, pointer);

            nlohmann::json result;
            result["command"] = "line.new";
            result["changed"] = true;
            result["line"] = id;
            result["status"] = "draft";
            result["path"] = core::sele::line_file(ctx.lay, id).string();

            std::ostringstream text;
            text << "Created line " << id << " (draft)\n"
                 << "  goal: " << ctx.line.goal << '\n'
                 << "  modules: " << comma_list(ctx.line.modules) << '\n'
                 << "  acceptance: " << ctx.line.acceptance.size() << '\n'
                 << "  next: sele task add " << id << " \"...\" --modules " << ctx.line.modules.front();
            output.print_both(text.str(), result);
        });
    });

    // line list
    struct ListArgs {
        std::string status;
    };
    auto list_args = std::make_shared<ListArgs>();
    auto* cmd_list = group->add_subcommand("list", "List lines");
    add_line_options(cmd_list, global_opts);
    cmd_list->add_option("--status", list_args->status, "Filter by status");
    cmd_list->footer("Example:\n  sele line list\n  sele line list --status active");
    cmd_list->callback([&global_opts, &exit_code, list_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            nlohmann::json items = nlohmann::json::array();
            std::ostringstream text;
            int shown = 0;

            if (core::fsys::exists(ctx.lay.lines)) {
                std::vector<fs::path> directories;
                for (const auto& entry : fs::directory_iterator(ctx.lay.lines)) {
                    if (entry.is_directory()) directories.push_back(entry.path());
                }
                std::sort(directories.begin(), directories.end());
                for (const auto& directory : directories) {
                    const auto file = directory / "line.json";
                    if (!core::fsys::exists(file)) continue;
                    const auto line = core::model::line_from_json(core::sele::read_json(file));
                    if (!list_args->status.empty() && line.status != list_args->status) continue;

                    core::RuntimeState runtime;
                    const auto state_path = core::sele::state_file(ctx.lay, line.id);
                    if (core::fsys::exists(state_path)) {
                        runtime = core::model::runtime_from_json(core::sele::read_json(state_path));
                    }
                    int done = 0;
                    int total = 0;
                    const auto task_dir = core::sele::tasks_dir(ctx.lay, line.id);
                    if (core::fsys::exists(task_dir)) {
                        for (const auto& entry : fs::directory_iterator(task_dir)) {
                            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
                            ++total;
                            const auto task = core::model::task_from_json(core::sele::read_json(entry.path()));
                            const auto found = runtime.tasks.find(task.id);
                            if (found != runtime.tasks.end() && found->second.status == "done") ++done;
                        }
                    }
                    const auto summary = core::model::summarize(line, runtime);

                    nlohmann::json item;
                    item["id"] = line.id;
                    item["status"] = line.status;
                    item["goal"] = line.goal;
                    item["tasks"] = nlohmann::json::object({{"done", done}, {"total", total}});
                    item["acceptance"] = nlohmann::json::object({
                        {"pass", summary.passed},
                        {"fail", summary.failed},
                        {"total", static_cast<int>(line.acceptance.size())}});
                    items.push_back(item);

                    text << line.status << "  " << line.id << "  " << done << '/' << total
                         << " tasks  " << summary.passed << '/'
                         << static_cast<int>(line.acceptance.size()) << " acceptance  " << line.goal
                         << '\n';
                    ++shown;
                }
            }
            if (shown == 0) text << "(no lines)";

            nlohmann::json result;
            result["command"] = "line.list";
            result["items"] = items;
            output.print_both(text.str(), result);
        });
    });

    // line show
    struct ShowArgs {
        std::string id;
    };
    auto show_args = std::make_shared<ShowArgs>();
    auto* cmd_show = group->add_subcommand("show", "Show a line with tasks and acceptance");
    add_line_options(cmd_show, global_opts);
    cmd_show->add_option("id", show_args->id, "Line id (default: session pointer)");
    cmd_show->footer("Example:\n  sele line show 20261029-fix-login\n  sele line show");
    cmd_show->callback([&global_opts, &exit_code, show_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, show_args->id);
            const auto summary = core::model::summarize(ctx.line, ctx.rt);

            nlohmann::json tasks = nlohmann::json::array();
            std::ostringstream text;
            text << "Line: " << ctx.line.id << " [" << ctx.line.status << "]\n"
                 << "Goal: " << ctx.line.goal << '\n'
                 << "Modules: " << comma_list(ctx.line.modules) << "\n\nTasks:";

            for (const auto& spec : ctx.tasks) {
                const std::string status = ng::status_of(ctx, spec.id);
                nlohmann::json item;
                item["id"] = spec.id;
                item["title"] = spec.title;
                item["status"] = status;
                item["modules"] = spec.modules;
                item["checkpoint"] = ctx.task_state(spec.id).checkpoint;
                tasks.push_back(item);
                text << "\n  [" << status << "] " << spec.id << ' ' << spec.title;
                if (status == "failed" || status == "blocked") {
                    text << " (" << ctx.task_state(spec.id).reason << ")";
                }
            }
            if (ctx.tasks.empty()) text << "\n  (none)";

            nlohmann::json acceptance = nlohmann::json::array();
            text << "\n\nAcceptance:";
            for (const auto& spec : ctx.line.acceptance) {
                const auto found = ctx.rt.acceptance.find(spec.id);
                const std::string status = found == ctx.rt.acceptance.end() ? "pending" : found->second.status;
                acceptance.push_back({{"id", spec.id}, {"desc", spec.desc}, {"check", spec.check},
                                      {"status", status}});
                text << "\n  [" << status << "] " << spec.id << ' ' << spec.desc;
            }
            if (ctx.line.acceptance.empty()) text << "\n  (none)";

            text << "\n\nBlockers: ";
            if (ctx.line.has_blocked) text << ctx.line.block_reason;
            else {
                std::vector<std::string> blockers;
                for (const auto& spec : ctx.tasks) {
                    const std::string status = ng::status_of(ctx, spec.id);
                    if (status == "blocked" || status == "failed") {
                        blockers.push_back(spec.id);
                    }
                }
                text << (blockers.empty() ? "none" : ng::join(blockers, ", "));
            }
            text << "\nAcceptance summary: " << summary.passed << " pass, " << summary.failed
                 << " fail, " << summary.pending << " pending";

            nlohmann::json result;
            result["command"] = "line.show";
            result["line"] = line_view(ctx.line);
            result["tasks"] = tasks;
            result["acceptance"] = acceptance;
            result["acceptance_summary"] = nlohmann::json::object(
                {{"pass", summary.passed}, {"fail", summary.failed}, {"pending", summary.pending}});
            output.print_both(text.str(), result);
        });
    });

    // line check
    struct CheckArgs {
        std::string id;
        bool strict = false;
    };
    auto check_args = std::make_shared<CheckArgs>();
    auto* cmd_check = group->add_subcommand("check", "Validate line structure and frozen acceptance");
    add_line_options(cmd_check, global_opts);
    cmd_check->add_option("id", check_args->id, "Line id (default: session pointer)");
    cmd_check->add_flag("--strict", check_args->strict, "Treat cross-line module overlap as failure");
    cmd_check->footer("Example:\n  sele line check 20261029-fix-login\n  sele line check --strict");
    cmd_check->callback([&global_opts, &exit_code, check_args]() {
        util::Output output(global_opts.output_format);
        bool check_passed = true;
        const int code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, check_args->id);
            nlohmann::json checks = nlohmann::json::array();
            bool passed = true;

            auto add = [&](const std::string& name, bool ok, const std::string& detail) {
                checks.push_back({{"name", name}, {"passed", ok}, {"detail", detail}});
                if (!ok) passed = false;
            };

            std::vector<std::string> errors;
            ng::check_linear(ctx.tasks, errors);
            add("linear_chain", errors.empty(), ng::join(errors, "; "));

            bool frozen_ok = true;
            if (ctx.line.has_frozen &&
                (ctx.line.status == "active" || ctx.line.status == "blocked" ||
                 ctx.line.status == "done")) {
                frozen_ok = core::goal_digest(ctx.line) == ctx.line.goal_sha256 &&
                            core::acceptance_digest(ctx.line) == ctx.line.acceptance_sha256;
            }
            add("frozen_acceptance", frozen_ok,
                frozen_ok ? "goal/acceptance unchanged" : "goal or acceptance changed after open");

            bool modules_ok = true;
            if (!ctx.conf.modules_allow.empty()) {
                for (const auto& module : ctx.line.modules) {
                    if (std::find(ctx.conf.modules_allow.begin(), ctx.conf.modules_allow.end(),
                                  module) == ctx.conf.modules_allow.end()) {
                        modules_ok = false;
                    }
                }
            }
            add("module_registry", modules_ok,
                modules_ok ? "all modules registered" : "unregistered module tag");

            bool done_ok = true;
            std::string done_detail = "n/a";
            if (ctx.line.status == "done" || ctx.line.status == "merged") {
                const int all_tasks = static_cast<int>(ctx.tasks.size());
                const int done_tasks = ng::count_tasks(ctx, "done");
                const int all_acc = static_cast<int>(ctx.line.acceptance.size());
                const int pass_acc = ng::count_acceptance(ctx, "pass");
                done_ok = all_tasks > 0 && done_tasks == all_tasks && pass_acc == all_acc;
                done_detail = std::to_string(done_tasks) + '/' + std::to_string(all_tasks) +
                              " tasks, " + std::to_string(pass_acc) + '/' +
                              std::to_string(all_acc) + " acceptance";
            }
            add("done_consistency", done_ok, done_detail);

            bool cross_ok = true;
            std::vector<std::string> overlaps;
            if (core::fsys::exists(ctx.lay.lines)) {
                for (const auto& entry : fs::directory_iterator(ctx.lay.lines)) {
                    if (!entry.is_directory()) continue;
                    const auto other_id = entry.path().filename().string();
                    if (other_id == ctx.line_id) continue;
                    const auto other_file = entry.path() / "line.json";
                    if (!core::fsys::exists(other_file)) continue;
                    const auto other = core::model::line_from_json(core::sele::read_json(other_file));
                    if (other.status != "active") continue;
                    for (const auto& module : ctx.line.modules) {
                        if (std::find(other.modules.begin(), other.modules.end(), module) !=
                            other.modules.end()) {
                            overlaps.push_back(module + " with " + other_id);
                        }
                    }
                }
            }
            const bool has_overlap = !overlaps.empty();
            if (has_overlap && check_args->strict) cross_ok = false;
            add("cross_line_modules", cross_ok,
                !has_overlap ? "no overlap"
                             : (check_args->strict ? ng::join(overlaps, "; ")
                                                   : "warning: " + ng::join(overlaps, "; ")));

            std::ostringstream text;
            text << (passed ? "Check passed" : "Check failed");
            for (const auto& check : checks) {
                text << "\n  [" << (check["passed"].get<bool>() ? "ok" : "fail") << "] "
                     << check["name"].get<std::string>();
                const auto detail = check["detail"].get<std::string>();
                if (!detail.empty()) text << " (" << detail << ')';
            }

            nlohmann::json result;
            result["command"] = "line.check";
            result["line"] = ctx.line_id;
            result["passed"] = passed;
            result["checks"] = checks;
            output.print_both(text.str(), result);
            check_passed = passed;
        });
        exit_code =
            (code == exit_codes::SUCCESS && !check_passed) ? exit_codes::LINT_FAILED : code;
    });

    // line open
    struct OpenArgs {
        std::string id;
    };
    auto open_args = std::make_shared<OpenArgs>();
    auto* cmd_open = group->add_subcommand("open", "Activate a draft line and freeze acceptance");
    add_line_options(cmd_open, global_opts);
    cmd_open->add_option("id", open_args->id, "Line id (default: session pointer)");
    cmd_open->footer("Example:\n  sele line open 20261029-fix-login");
    cmd_open->callback([&global_opts, &exit_code, open_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, open_args->id);

            if (ctx.line.status == "active") {
                nlohmann::json result;
                result["command"] = "line.open";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["status"] = "active";
                output.print_both("Line " + ctx.line_id + " is already active (unchanged)", result);
                return;
            }
            if (ctx.line.status != "draft") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + ", not draft",
                     "Only draft lines can be opened", {"sele line show " + ctx.line_id});
            }

            std::vector<std::string> errors;
            ng::check_linear(ctx.tasks, errors);
            if (!errors.empty()) {
                fail(exit_codes::STATE_CONFLICT, "Line has no valid task chain: " + errors.front(),
                     "Add tasks first", {"sele task add " + ctx.line_id + " \"...\" --modules " +
                                             ctx.line.modules.front()});
            }
            if (ctx.line.acceptance.empty()) {
                fail(exit_codes::STATE_CONFLICT, "Line has no acceptance items", "",
                     {"sele line amend " + ctx.line_id + " --reason \"add acceptance\""});
            }
            if (ctx.line.modules.empty()) {
                fail(exit_codes::STATE_CONFLICT, "Line has no module tags", "");
            }
            if (!core::gitrt::is_repo(ctx.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Not a git repository: " + ctx.lay.root.string(),
                     "Run git init first");
            }
            if (core::gitrt::is_dirty(ctx.lay.root)) {
                fail(exit_codes::STATE_CONFLICT, "Working tree has uncommitted code changes",
                     "Commit or stash them before opening a line",
                     {"git status", "sele task list " + ctx.line_id});
            }
            if (!core::gitrt::has_head(ctx.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Git repository has no commits",
                     "Create an initial commit before opening a line");
            }

            const std::string branch = branch_for(ctx.line_id);
            if (core::gitrt::current_branch(ctx.lay.root).rfind("sele/", 0) != 0) {
                core::gitrt::commit_all(ctx.lay.root,
                                        "chore(sele): plan " + ctx.line_id);
            }
            ctx.line.base_commit = core::gitrt::head_short(ctx.lay.root);
            if (core::gitrt::branch_exists(ctx.lay.root, branch)) {
                core::gitrt::switch_branch(ctx.lay.root, branch);
            } else {
                core::gitrt::create_branch(ctx.lay.root, branch);
            }

            ctx.line.has_frozen = true;
            ctx.line.goal_sha256 = core::goal_digest(ctx.line);
            ctx.line.acceptance_sha256 = core::acceptance_digest(ctx.line);
            ctx.line.frozen_at = core::sele::utc_now();
            ctx.line.status = "active";
            ng::save_line(ctx);
            ng::append_log(ctx, "line.open", {{"frozen", ctx.line.acceptance_sha256}});

            core::session::Pointer pointer;
            pointer.line = ctx.line_id;
            core::session::write(ctx.lay, ctx.session, pointer);

            nlohmann::json result;
            result["command"] = "line.open";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["status"] = "active";
            result["branch"] = branch;
            result["frozen"] = ctx.line.acceptance_sha256;

            std::ostringstream text;
            text << "Opened " << ctx.line_id << " on " << branch << '\n'
                 << "  frozen acceptance: " << ctx.line.acceptance_sha256.substr(0, 12) << "...\n"
                 << "  next: sele task start " << ctx.line_id << ' ' << ctx.tasks.front().id;
            output.print_both(text.str(), result);
        });
    });

    // line switch
    struct SwitchArgs {
        std::string id;
    };
    auto switch_args = std::make_shared<SwitchArgs>();
    auto* cmd_switch = group->add_subcommand("switch", "Switch the session to a line");
    add_line_options(cmd_switch, global_opts);
    cmd_switch->add_option("id", switch_args->id, "Line id")->required();
    cmd_switch->footer("Example:\n  sele line switch 20261029-fix-login");
    cmd_switch->callback([&global_opts, &exit_code, switch_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, switch_args->id);
            if (ctx.line.status == "done" || ctx.line.status == "abandoned" ||
                ctx.line.status == "merged") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + " and cannot be switched to",
                     "Done or merged lines are read-only", {"sele line show " + ctx.line_id});
            }
            if (!core::gitrt::is_repo(ctx.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Not a git repository: " + ctx.lay.root.string(),
                     "Run git init first");
            }
            if (core::gitrt::is_dirty(ctx.lay.root)) {
                fail(exit_codes::STATE_CONFLICT, "Working tree has uncommitted code changes",
                     "Commit or stash them before switching", {"git status"});
            }

            const std::string branch = branch_for(ctx.line_id);
            if (core::gitrt::branch_exists(ctx.lay.root, branch)) {
                core::gitrt::switch_branch(ctx.lay.root, branch);
            } else {
                core::gitrt::create_branch(ctx.lay.root, branch);
            }

            core::session::Pointer pointer;
            pointer.line = ctx.line_id;
            core::session::write(ctx.lay, ctx.session, pointer);
            ng::append_event(ctx, "line.switch");

            nlohmann::json result;
            result["command"] = "line.switch";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["branch"] = branch;
            result["session"] = ctx.session;
            output.print_both("Switched session '" + ctx.session + "' to " + ctx.line_id + " (" +
                                  branch + ")",
                              result);
        });
    });

    // line done
    struct DoneArgs {
        std::string id;
        bool no_commit = false;
    };
    auto done_args = std::make_shared<DoneArgs>();
    auto* cmd_done = group->add_subcommand("done", "Finish a line when all tasks and acceptance pass");
    add_line_options(cmd_done, global_opts);
    cmd_done->add_option("id", done_args->id, "Line id (default: session pointer)");
    cmd_done->add_flag("--no-commit", done_args->no_commit,
                       "Leave .sele metadata uncommitted (default: commit it)");
    cmd_done->footer("Example:\n  sele line done 20261029-fix-login\n  sele line done --no-commit");
    cmd_done->callback([&global_opts, &exit_code, done_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, done_args->id);
            ng::require_active(ctx, "line done");

            if (ctx.tasks.empty() || ng::count_tasks(ctx, "done") != static_cast<int>(ctx.tasks.size())) {
                fail(exit_codes::STATE_CONFLICT,
                     "Not all tasks are done (" + std::to_string(ng::count_tasks(ctx, "done")) + '/' +
                         std::to_string(ctx.tasks.size()) + ")",
                     "Finish or block remaining tasks", {"sele task list " + ctx.line_id});
            }
            if (ng::count_acceptance(ctx, "pass") != static_cast<int>(ctx.line.acceptance.size())) {
                fail(exit_codes::STATE_CONFLICT,
                     "Not all acceptance items pass (" +
                         std::to_string(ng::count_acceptance(ctx, "pass")) + '/' +
                         std::to_string(ctx.line.acceptance.size()) + ")",
                     "Record results with sele acceptance pass|fail",
                     {"sele acceptance pass " + ctx.line_id + " <A-id> --evidence \"...\""});
            }

            const auto summary = core::model::summarize(ctx.line, ctx.rt);
            ctx.line.status = "done";
            ctx.line.has_closed = true;
            ctx.line.closed_at = core::sele::utc_now();
            ctx.line.closed_pass = summary.passed;
            ctx.line.closed_fail = summary.failed;
            ng::save_line(ctx);
            ng::append_log(ctx, "line.done");

            bool committed = false;
            if (!done_args->no_commit && core::gitrt::is_repo(ctx.lay.root)) {
                if (core::gitrt::current_branch(ctx.lay.root) == branch_for(ctx.line_id)) {
                    committed = core::gitrt::commit_all(
                        ctx.lay.root, "chore(sele): line " + ctx.line_id + " done");
                }
            }

            nlohmann::json result;
            result["command"] = "line.done";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["status"] = "done";
            result["committed"] = committed;
            result["acceptance_summary"] = nlohmann::json::object(
                {{"pass", summary.passed}, {"fail", summary.failed}});

            std::ostringstream text;
            text << "Closed " << ctx.line_id << " (done)\n"
                 << "  acceptance: " << summary.passed << " pass, " << summary.failed << " fail";
            if (done_args->no_commit) {
                text << "\n  metadata: uncommitted (--no-commit)";
            } else {
                text << "\n  metadata: " << (committed ? "committed" : "nothing to commit");
            }
            output.print_both(text.str(), result);
        });
    });

    // line block / unblock
    struct ReasonArgs {
        std::string id;
        std::string reason;
    };
    auto block_args = std::make_shared<ReasonArgs>();
    auto* cmd_block = group->add_subcommand("block", "Mark a line blocked");
    add_line_options(cmd_block, global_opts);
    cmd_block->add_option("id", block_args->id, "Line id (default: session pointer)");
    cmd_block->add_option("--reason", block_args->reason, "Why the line is blocked")->required();
    cmd_block->footer("Example:\n  sele line block 20261029-fix-login --reason \"waiting for review\"");
    cmd_block->callback([&global_opts, &exit_code, block_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, block_args->id);
            if (ctx.line.status == "blocked") {
                nlohmann::json result;
                result["command"] = "line.block";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["status"] = "blocked";
                output.print_both("Line " + ctx.line_id + " is already blocked (unchanged)", result);
                return;
            }
            if (ctx.line.status != "active") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + ", not active",
                     "Only active lines can be blocked", {"sele line show " + ctx.line_id});
            }
            ctx.line.status = "blocked";
            ctx.line.has_blocked = true;
            ctx.line.block_reason = block_args->reason;
            ctx.line.blocked_at = core::sele::utc_now();
            ng::save_line(ctx);
            ng::append_log(ctx, "line.block", {{"reason", block_args->reason}});

            nlohmann::json result;
            result["command"] = "line.block";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["status"] = "blocked";
            result["reason"] = block_args->reason;
            output.print_both("Blocked " + ctx.line_id + ": " + block_args->reason, result);
        });
    });

    struct IdArgs {
        std::string id;
    };
    auto unblock_args = std::make_shared<IdArgs>();
    auto* cmd_unblock = group->add_subcommand("unblock", "Resume a blocked line");
    add_line_options(cmd_unblock, global_opts);
    cmd_unblock->add_option("id", unblock_args->id, "Line id (default: session pointer)");
    cmd_unblock->footer("Example:\n  sele line unblock 20261029-fix-login");
    cmd_unblock->callback([&global_opts, &exit_code, unblock_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, unblock_args->id);
            if (ctx.line.status == "active") {
                nlohmann::json result;
                result["command"] = "line.unblock";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["status"] = "active";
                output.print_both("Line " + ctx.line_id + " is already active (unchanged)", result);
                return;
            }
            if (ctx.line.status != "blocked") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + ", not blocked",
                     "Only blocked lines can be unblocked", {"sele line show " + ctx.line_id});
            }
            ctx.line.status = "active";
            ctx.line.has_blocked = false;
            ctx.line.block_reason.clear();
            ctx.line.blocked_at.clear();
            ng::save_line(ctx);
            ng::append_log(ctx, "line.unblock");

            nlohmann::json result;
            result["command"] = "line.unblock";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["status"] = "active";
            output.print_both("Unblocked " + ctx.line_id, result);
        });
    });

    // line reopen
    struct ReopenArgs {
        std::string id;
        std::string reason;
    };
    auto reopen_args = std::make_shared<ReopenArgs>();
    auto* cmd_reopen = group->add_subcommand("reopen", "Reopen a done line");
    add_line_options(cmd_reopen, global_opts);
    cmd_reopen->add_option("id", reopen_args->id, "Line id (default: session pointer)");
    cmd_reopen->add_option("--reason", reopen_args->reason, "Why the line is reopened")->required();
    cmd_reopen->footer("Example:\n  sele line reopen 20261029-fix-login --reason \"regression\"");
    cmd_reopen->callback([&global_opts, &exit_code, reopen_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, reopen_args->id);
            if (ctx.line.status == "active") {
                nlohmann::json result;
                result["command"] = "line.reopen";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["status"] = "active";
                output.print_both("Line " + ctx.line_id + " is already active (unchanged)", result);
                return;
            }
            if (ctx.line.status != "done") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + ", not done",
                     "Only done lines can be reopened", {"sele line show " + ctx.line_id});
            }
            ctx.line.status = "active";
            ctx.line.has_closed = false;
            ctx.line.closed_at.clear();
            ng::save_line(ctx);
            ng::append_log(ctx, "line.reopen", {{"reason", reopen_args->reason}});

            nlohmann::json result;
            result["command"] = "line.reopen";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["status"] = "active";
            output.print_both("Reopened " + ctx.line_id + ": " + reopen_args->reason, result);
        });
    });

    // line abandon
    struct AbandonArgs {
        std::string id;
        std::string reason;
    };
    auto abandon_args = std::make_shared<AbandonArgs>();
    auto* cmd_abandon = group->add_subcommand("abandon", "Abandon a line, keeping its record");
    add_line_options(cmd_abandon, global_opts);
    cmd_abandon->add_option("id", abandon_args->id, "Line id (default: session pointer)");
    cmd_abandon->add_option("--reason", abandon_args->reason, "Why the line is abandoned")->required();
    cmd_abandon->footer("Example:\n  sele line abandon 20261029-fix-login --reason \"superseded\"");
    cmd_abandon->callback([&global_opts, &exit_code, abandon_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, abandon_args->id);
            if (ctx.line.status == "abandoned") {
                nlohmann::json result;
                result["command"] = "line.abandon";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["status"] = "abandoned";
                output.print_both("Line " + ctx.line_id + " is already abandoned (unchanged)", result);
                return;
            }
            if (ctx.line.status == "done" || ctx.line.status == "merged") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + ", not abandoned",
                     "Done or merged lines are read-only",
                     {"sele line show " + ctx.line_id});
            }
            ctx.line.status = "abandoned";
            ng::save_line(ctx);
            ng::append_log(ctx, "line.abandon", {{"reason", abandon_args->reason}});

            nlohmann::json result;
            result["command"] = "line.abandon";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["status"] = "abandoned";
            output.print_both("Abandoned " + ctx.line_id + ": " + abandon_args->reason, result);
        });
    });

    // line amend
    struct AmendArgs {
        std::string id;
        std::string reason;
        std::string goal;
        std::string modules;
        std::vector<std::string> acceptance;
    };
    auto amend_args = std::make_shared<AmendArgs>();
    auto* cmd_amend = group->add_subcommand("amend", "Change goal/acceptance/modules with a reason");
    add_line_options(cmd_amend, global_opts);
    cmd_amend->add_option("id", amend_args->id, "Line id (default: session pointer)");
    cmd_amend->add_option("--reason", amend_args->reason, "Why the plan changed")->required();
    cmd_amend->add_option("--goal", amend_args->goal, "New goal");
    cmd_amend->add_option("--acceptance", amend_args->acceptance, "Replacement acceptance list");
    cmd_amend->add_option("--modules", amend_args->modules, "Replacement module tags");
    cmd_amend->footer("Example:\n  sele line amend 20261029-fix-login --reason \"scope shrank\" "
                      "--goal \"Login rejects bad credentials\"");
    cmd_amend->callback([&global_opts, &exit_code, amend_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load(global_opts, amend_args->id);
            if (ctx.line.status == "done" || ctx.line.status == "abandoned" ||
                ctx.line.status == "merged") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + " and cannot be amended",
                     "Reopen a done line first", {"sele line show " + ctx.line_id});
            }
            if (amend_args->goal.empty() && amend_args->acceptance.empty() &&
                amend_args->modules.empty()) {
                fail(exit_codes::USAGE_ERROR, "Nothing to amend",
                     "Pass at least one of --goal/--acceptance/--modules");
            }

            const std::string old_frozen = ctx.line.acceptance_sha256;
            if (!amend_args->goal.empty()) {
                if (amend_args->goal.size() > 120) {
                    fail(exit_codes::USAGE_ERROR, "Goal must be <= 120 characters", "");
                }
                ctx.line.goal = amend_args->goal;
            }
            if (!amend_args->modules.empty()) {
                const auto modules = ng::split_csv(amend_args->modules);
                if (modules.empty()) fail(exit_codes::USAGE_ERROR, "--modules is empty", "");
                for (const auto& module : modules) {
                    if (!valid_module(module)) {
                        fail(exit_codes::USAGE_ERROR, "Invalid module tag: " + module, "");
                    }
                }
                ctx.line.modules = modules;
            }
            if (!amend_args->acceptance.empty()) {
                std::vector<core::Acceptance> acceptance;
                for (std::size_t i = 0; i < amend_args->acceptance.size(); ++i) {
                    const std::string& raw = amend_args->acceptance[i];
                    const auto split = raw.find("::");
                    if (split == std::string::npos) {
                        fail(exit_codes::USAGE_ERROR, "Invalid --acceptance (missing ::): " + raw,
                             "Format: \"<desc>::<check>\"");
                    }
                    const std::string desc = raw.substr(0, split);
                    const std::string check = raw.substr(split + 2);
                    if (desc.empty() || check.empty()) {
                        fail(exit_codes::USAGE_ERROR, "Empty acceptance desc or check: " + raw, "");
                    }
                    acceptance.push_back(core::Acceptance{"A" + std::to_string(i + 1), desc, check});
                }
                ctx.line.acceptance = acceptance;
                // Drop recorded results that no longer map to an item.
                for (auto it = ctx.rt.acceptance.begin(); it != ctx.rt.acceptance.end();) {
                    bool found = false;
                    for (const auto& item : ctx.line.acceptance) {
                        if (item.id == it->first) found = true;
                    }
                    if (!found) it = ctx.rt.acceptance.erase(it);
                    else ++it;
                }
                ng::save_runtime(ctx);
            }

            if (ctx.line.has_frozen) {
                ctx.line.goal_sha256 = core::goal_digest(ctx.line);
                ctx.line.acceptance_sha256 = core::acceptance_digest(ctx.line);
                ctx.line.frozen_at = core::sele::utc_now();
            }
            ng::save_line(ctx);
            ng::append_log(ctx, "line.amend",
                           {{"reason", amend_args->reason}, {"old_frozen", old_frozen},
                            {"frozen", ctx.line.acceptance_sha256}});

            nlohmann::json result;
            result["command"] = "line.amend";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["reason"] = amend_args->reason;
            result["frozen"] = ctx.line.acceptance_sha256;
            output.print_both("Amended " + ctx.line_id + ": " + amend_args->reason, result);
        });
    });

    // line merge
    struct MergeArgs {
        std::string id;
        bool confirm = false;
        std::string onto = "main";
        bool delete_branch = false;
    };
    auto merge_args = std::make_shared<MergeArgs>();
    auto* cmd_merge = group->add_subcommand("merge", "Merge a done line into its base branch");
    add_line_options(cmd_merge, global_opts);
    cmd_merge->add_option("id", merge_args->id, "Line id (default: session pointer)");
    cmd_merge->add_flag("--confirm", merge_args->confirm, "Confirm the merge");
    cmd_merge->add_option("--onto", merge_args->onto, "Base branch (default: main)");
    cmd_merge->add_flag("--delete-branch", merge_args->delete_branch,
                        "Delete sele/<id> after a successful merge");
    cmd_merge->footer("Example:\n  sele line merge 20261029-fix-login --confirm\n"
                      "  sele line merge --confirm --onto main");
    cmd_merge->callback([&global_opts, &exit_code, merge_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto root = ng::load_root(global_opts);
            std::string line_id = merge_args->id;
            if (line_id.empty()) {
                line_id = core::session::read(root.lay, root.session).line;
            }
            if (line_id.empty()) {
                fail(exit_codes::STATE_CONFLICT,
                     "No active line for session '" + root.session + "'",
                     "Switch to a line first", {"sele line list"});
            }
            if (!core::sele::valid_line_id(line_id)) {
                fail(exit_codes::USAGE_ERROR, "Invalid line id: " + line_id,
                     "Line ids look like <YYYYMMDD>-<slug>", {"sele line list"});
            }
            if (!core::gitrt::is_repo(root.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Not a git repository: " + root.lay.root.string(), "");
            }
            if (!core::gitrt::has_head(root.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Git repository has no commits", "");
            }
            if (core::gitrt::is_dirty(root.lay.root)) {
                fail(exit_codes::STATE_CONFLICT, "Working tree has uncommitted code changes",
                     "Commit or stash them before merging", {"git status"});
            }
            const std::string branch = branch_for(line_id);
            if (!core::gitrt::branch_exists(root.lay.root, branch)) {
                fail(exit_codes::STATE_CONFLICT, "Line branch not found: " + branch,
                     "Open the line to create its branch", {"sele line open " + line_id});
            }
            if (!core::gitrt::branch_exists(root.lay.root, merge_args->onto)) {
                fail(exit_codes::ENV_ERROR, "Base branch not found: " + merge_args->onto,
                     "Pass --onto <branch>", {"git branch --all"});
            }
            if (core::gitrt::current_branch(root.lay.root) != branch) {
                core::gitrt::switch_branch(root.lay.root, branch);
            }

            auto ctx = ng::load(global_opts, line_id);

            if (ctx.line.status == "merged") {
                nlohmann::json result;
                result["command"] = "line.merge";
                result["changed"] = false;
                result["line"] = ctx.line_id;
                result["status"] = "merged";
                result["commit"] = ctx.line.merge_commit;
                output.print_both("Line " + ctx.line_id + " is already merged (unchanged)", result);
                return;
            }
            if (ctx.line.status != "done") {
                fail(exit_codes::STATE_CONFLICT,
                     "Line " + ctx.line_id + " is " + ctx.line.status + ", not done",
                     "Finish the line before merging", {"sele line done " + ctx.line_id});
            }
            if (!merge_args->confirm) {
                fail(exit_codes::USAGE_ERROR, "merge requires --confirm",
                     "Review the line, then re-run with --confirm",
                     {"sele line show " + ctx.line_id});
            }
            if (ctx.line.base_commit.empty()) {
                const auto base_head =
                    core::gitrt::run(ctx.lay.root, {"rev-parse", "--short", merge_args->onto});
                if (base_head.code == 0) ctx.line.base_commit = trim_text(base_head.out);
            }

            ctx.line.status = "merged";
            ctx.line.has_merged = true;
            ctx.line.merged_at = core::sele::utc_now();
            ctx.line.merge_commit.clear();
            ng::save_line(ctx);
            core::gitrt::commit_all(ctx.lay.root,
                                    "chore(sele): mark " + ctx.line_id + " merged");

            core::gitrt::switch_branch(ctx.lay.root, merge_args->onto);
            const auto merge = core::gitrt::run(
                ctx.lay.root, {"merge", "--no-ff", "-m", "sele: merge " + ctx.line_id, branch});
            if (merge.code != 0) {
                const auto conflicts =
                    core::gitrt::run(ctx.lay.root, {"diff", "--name-only", "--diff-filter=U"});
                core::gitrt::run(ctx.lay.root, {"merge", "--abort"});
                core::gitrt::switch_branch(ctx.lay.root, branch);

                ctx.line.status = "done";
                ctx.line.has_merged = false;
                ctx.line.merged_at.clear();
                ctx.line.merge_commit.clear();
                ng::save_line(ctx);
                core::gitrt::commit_all(ctx.lay.root,
                                        "chore(sele): unmark " + ctx.line_id + " merged");

                const std::string detail = trim_text(conflicts.out);
                fail(exit_codes::STATE_CONFLICT,
                     "Merge conflict in " + ctx.line_id + (detail.empty() ? "" : ": " + detail),
                     "Resolve it on " + branch + " and retry",
                     {"git switch " + branch, "git merge " + merge_args->onto,
                      "sele line merge " + ctx.line_id + " --confirm"});
            }

            ctx.line.merge_commit = core::gitrt::head_short(ctx.lay.root);
            ng::append_log(ctx, "line.merge",
                           {{"commit", ctx.line.merge_commit}, {"base", merge_args->onto}});
            ng::save_line(ctx);
            core::gitrt::commit_all(ctx.lay.root,
                                    "chore(sele): record merge " + ctx.line_id);

            if (merge_args->delete_branch) {
                core::gitrt::run(ctx.lay.root, {"branch", "-d", branch});
            }

            nlohmann::json result;
            result["command"] = "line.merge";
            result["changed"] = true;
            result["line"] = ctx.line_id;
            result["status"] = "merged";
            result["commit"] = ctx.line.merge_commit;
            result["base"] = merge_args->onto;
            result["branch_deleted"] = merge_args->delete_branch;

            std::ostringstream text;
            text << "Merged " << ctx.line_id << " into " << merge_args->onto << " at "
                 << ctx.line.merge_commit;
            if (merge_args->delete_branch) text << "\n  branch: " << branch << " deleted";
            output.print_both(text.str(), result);
        });
    });
}

} // namespace cli
