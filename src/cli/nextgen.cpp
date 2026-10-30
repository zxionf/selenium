#include "cli/nextgen.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>

#include "core/fs.hpp"
#include "core/journal.hpp"
#include "core/session.hpp"

namespace fs = std::filesystem;

namespace cli::ng {

namespace {

bool is_dir(const fs::path& path) {
    std::error_code ec;
    return fs::is_directory(path, ec);
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    return value.substr(begin, end - begin);
}

int task_number(const std::string& id) {
    if (id.size() < 2 || id[0] != 'T') return 0;
    int value = 0;
    for (std::size_t i = 1; i < id.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(id[i]))) return 0;
        value = value * 10 + (id[i] - '0');
    }
    return value;
}

} // namespace

[[noreturn]] void fail(int code, std::string message, std::string suggestion,
                       std::vector<std::string> next) {
    throw CliError{code, std::move(message), std::move(suggestion), std::move(next)};
}

core::TaskSpec* Ctx::task(const std::string& id) {
    for (auto& item : tasks) {
        if (item.id == id) return &item;
    }
    return nullptr;
}

bool Ctx::has_task(const std::string& id) const {
    for (const auto& item : tasks) {
        if (item.id == id) return true;
    }
    return false;
}

core::TaskState& Ctx::task_state(const std::string& id) {
    return rt.tasks[id];
}

core::AcceptanceState& Ctx::acc_state(const std::string& id) {
    return rt.acceptance[id];
}

core::sele::Layout require_layout(const GlobalOptions& opts) {
    const auto start = opts.root.empty() ? fs::current_path() : opts.root;
    const auto root = core::sele::find_root(start);
    if (!root) {
        fail(exit_codes::ENV_ERROR, "No .sele directory found",
             "Run sele init in the project root", {"sele init"});
    }
    return core::sele::layout(*root);
}

Ctx load_root(const GlobalOptions& opts) {
    Ctx ctx;
    ctx.lay = require_layout(opts);
    ctx.conf = core::sele::load_config(ctx.lay);
    ctx.actor = core::sele::resolve_actor(ctx.conf);
    ctx.session = core::session::resolve_id(opts.session);
    return ctx;
}

Ctx load(const GlobalOptions& opts, const std::string& explicit_line) {
    Ctx ctx = load_root(opts);
    const auto pointer = core::session::read(ctx.lay, ctx.session);
    ctx.line_id = explicit_line.empty() ? pointer.line : explicit_line;

    if (ctx.line_id.empty()) {
        fail(exit_codes::STATE_CONFLICT,
             "No active line for session '" + ctx.session + "'",
             "Switch to a line first",
             {"sele line list", "sele line switch <id>"});
    }
    if (!core::sele::valid_line_id(ctx.line_id)) {
        fail(exit_codes::USAGE_ERROR, "Invalid line id: " + ctx.line_id,
             "Line ids look like <YYYYMMDD>-<slug>", {"sele line list"});
    }

    const auto active_dir = core::sele::line_dir(ctx.lay, ctx.line_id);
    fs::path base_dir = active_dir;
    if (!core::fsys::exists(active_dir / "line.json")) {
        const auto archived_dir = ctx.lay.sele / "archive" / ctx.line_id;
        if (core::fsys::exists(archived_dir / "line.json")) {
            base_dir = archived_dir;
            ctx.archived = true;
        }
    }
    const auto line_path = base_dir / "line.json";
    if (!core::fsys::exists(line_path)) {
        fail(exit_codes::STATE_CONFLICT, "Line not found: " + ctx.line_id, "List available lines",
             {"sele line list"});
    }
    ctx.line = core::model::line_from_json(core::sele::read_json(line_path));

    const auto state_path = core::sele::state_file(ctx.lay, ctx.line_id);
    if (core::fsys::exists(state_path)) {
        ctx.rt = core::model::runtime_from_json(core::sele::read_json(state_path));
    } else {
        ctx.rt = core::RuntimeState{};
        ctx.rt.line = ctx.line_id;
    }

    const auto directory = base_dir / "tasks";
    if (is_dir(directory)) {
        for (const auto& entry : fs::directory_iterator(directory)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            ctx.tasks.push_back(core::model::task_from_json(core::sele::read_json(entry.path())));
        }
        std::sort(ctx.tasks.begin(), ctx.tasks.end(), [](const auto& left, const auto& right) {
            return task_number(left.id) < task_number(right.id);
        });
    }
    return ctx;
}

void save_line(Ctx& ctx) {
    if (ctx.archived) {
        fail(exit_codes::STATE_CONFLICT,
             "Line " + ctx.line_id + " is archived and read-only", "",
             {"sele line show " + ctx.line_id});
    }
    const auto path = core::sele::line_file(ctx.lay, ctx.line_id);
    if (core::fsys::exists(path)) {
        const auto current = core::model::line_from_json(core::sele::read_json(path));
        if (current.version != ctx.line.version) {
            fail(exit_codes::STATE_CONFLICT,
                 "line.json was modified by another process (version " +
                     std::to_string(current.version) + " != " + std::to_string(ctx.line.version) +
                     ")",
                 "Re-read the line and retry", {"sele line show " + ctx.line_id});
        }
    }
    ctx.line.version += 1;
    core::sele::write_json(path, core::model::to_json(ctx.line));
}

void save_runtime(Ctx& ctx) {
    if (ctx.archived) {
        fail(exit_codes::STATE_CONFLICT,
             "Line " + ctx.line_id + " is archived and read-only", "",
             {"sele line show " + ctx.line_id});
    }
    core::fsys::ensure_dir(ctx.lay.state);
    const auto path = core::sele::state_file(ctx.lay, ctx.line_id);
    if (core::fsys::exists(path)) {
        const auto current = core::model::runtime_from_json(core::sele::read_json(path));
        if (current.version != ctx.rt.version) {
            fail(exit_codes::STATE_CONFLICT,
                 "runtime state was modified by another process (version " +
                     std::to_string(current.version) + " != " + std::to_string(ctx.rt.version) +
                     ")",
                 "Re-read the line and retry", {"sele line show " + ctx.line_id});
        }
    }
    ctx.rt.version += 1;
    ctx.rt.line = ctx.line_id;
    core::sele::write_json(path, core::model::to_json(ctx.rt));
}

void save_task(Ctx& ctx, const core::TaskSpec& task) {
    if (ctx.archived) {
        fail(exit_codes::STATE_CONFLICT,
             "Line " + ctx.line_id + " is archived and read-only", "",
             {"sele line show " + ctx.line_id});
    }
    core::fsys::ensure_dir(core::sele::tasks_dir(ctx.lay, ctx.line_id));
    const auto path = core::sele::tasks_dir(ctx.lay, ctx.line_id) / (task.id + ".json");
    core::sele::write_json(path, core::model::to_json(task));
}

void append_log(Ctx& ctx, const std::string& op, json extra) {
    json entry = json::object();
    entry["ts"] = core::sele::utc_now();
    entry["actor"] = ctx.actor;
    entry["op"] = op;
    entry["line"] = ctx.line_id;
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        const std::string key = it.key();
        if (key == "ts" || key == "actor" || key == "op" || key == "line") continue;
        entry[key] = it.value();
    }
    core::journal::append(ctx.lay.log, entry);
}

void append_event(Ctx& ctx, const std::string& op, json extra) {
    json entry = json::object();
    entry["ts"] = core::sele::utc_now();
    entry["actor"] = ctx.actor;
    entry["op"] = op;
    entry["line"] = ctx.line_id;
    for (auto it = extra.begin(); it != extra.end(); ++it) {
        const std::string key = it.key();
        if (key == "ts" || key == "actor" || key == "op" || key == "line") continue;
        entry[key] = it.value();
    }
    core::journal::append(ctx.lay.events, entry);
}

std::string last_task_id(const Ctx& ctx) {
    return ctx.tasks.empty() ? std::string() : ctx.tasks.back().id;
}

std::string next_task_id(const Ctx& ctx) {
    int highest = 0;
    for (const auto& item : ctx.tasks) highest = std::max(highest, task_number(item.id));
    return "T" + std::to_string(highest + 1);
}

std::string status_of(const Ctx& ctx, const std::string& task_id) {
    const auto found = ctx.rt.tasks.find(task_id);
    return found == ctx.rt.tasks.end() ? "pending" : found->second.status;
}

int count_tasks(const Ctx& ctx, const std::string& status) {
    int count = 0;
    for (const auto& item : ctx.tasks) {
        if (status_of(ctx, item.id) == status) ++count;
    }
    return count;
}

int count_acceptance(const Ctx& ctx, const std::string& status) {
    int count = 0;
    for (const auto& item : ctx.line.acceptance) {
        const auto found = ctx.rt.acceptance.find(item.id);
        const std::string current = found == ctx.rt.acceptance.end() ? "pending" : found->second.status;
        if (current == status) ++count;
    }
    return count;
}

std::vector<std::string> split_csv(const std::string& text) {
    std::vector<std::string> out;
    std::size_t begin = 0;
    while (begin <= text.size()) {
        const std::size_t comma = text.find(',', begin);
        const std::string part =
            trim(text.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin));
        if (!part.empty()) out.push_back(part);
        if (comma == std::string::npos) break;
        begin = comma + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& values, const std::string& separator) {
    std::string out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out += separator;
        out += values[i];
    }
    return out;
}

void require_active(const Ctx& ctx, const std::string& action) {
    if (ctx.line.status == "active") return;
    std::vector<std::string> next;
    if (ctx.line.status == "draft") {
        next = {"sele line open " + ctx.line_id};
    } else if (ctx.line.status == "blocked") {
        next = {"sele line unblock " + ctx.line_id};
    } else {
        next = {"sele line show " + ctx.line_id};
    }
    fail(exit_codes::STATE_CONFLICT,
         "Line " + ctx.line_id + " is " + ctx.line.status + ", not active",
         action + " requires an active line", next);
}

void check_graph(const std::vector<core::TaskSpec>& tasks, std::vector<std::string>& errors,
                 std::vector<std::string>& warnings) {
    if (tasks.empty()) {
        errors.push_back("line has no tasks");
        return;
    }

    std::map<std::string, const core::TaskSpec*> by_id;
    for (const auto& task : tasks) by_id[task.id] = &task;

    std::size_t roots = 0;
    for (const auto& task : tasks) {
        if (task.depends.empty()) ++roots;
        for (const auto& dep : task.depends) {
            if (by_id.find(dep) == by_id.end()) {
                errors.push_back("task " + task.id + " depends on missing " + dep);
            }
        }
    }
    if (roots == 0) errors.push_back("task graph has no root");
    if (roots > 1) errors.push_back("task graph has multiple roots");

    // Group members must share the same predecessors; overlapping modules inside a
    // group are only a warning because worktrees isolate the actual writes.
    std::map<std::string, std::set<std::string>> group_deps;
    std::map<std::string, std::set<std::string>> group_modules;
    for (const auto& task : tasks) {
        if (task.group.empty()) continue;
        const std::set<std::string> deps(task.depends.begin(), task.depends.end());
        const auto found = group_deps.find(task.group);
        if (found == group_deps.end()) {
            group_deps[task.group] = deps;
        } else if (found->second != deps) {
            errors.push_back("group " + task.group + " members must share the same depends");
        }
        for (const auto& module : task.modules) {
            if (!group_modules[task.group].insert(module).second) {
                warnings.push_back("group " + task.group + " has overlapping module: " + module);
            }
        }
    }

    std::map<std::string, int> indegree;
    std::map<std::string, std::vector<std::string>> children;
    for (const auto& task : tasks) {
        indegree[task.id] += 0;
        for (const auto& dep : task.depends) {
            if (by_id.find(dep) == by_id.end()) continue;
            ++indegree[task.id];
            children[dep].push_back(task.id);
        }
    }

    std::vector<std::string> ready;
    for (const auto& entry : indegree) {
        if (entry.second == 0) ready.push_back(entry.first);
    }
    std::size_t visited = 0;
    while (!ready.empty()) {
        const std::string id = ready.back();
        ready.pop_back();
        ++visited;
        for (const auto& child : children[id]) {
            if (--indegree[child] == 0) ready.push_back(child);
        }
    }
    if (visited != tasks.size()) errors.push_back("task graph has a cycle");

    std::set<std::string> reachable;
    std::vector<std::string> stack;
    for (const auto& task : tasks) {
        if (task.depends.empty()) stack.push_back(task.id);
    }
    while (!stack.empty()) {
        const std::string id = stack.back();
        stack.pop_back();
        if (!reachable.insert(id).second) continue;
        for (const auto& child : children[id]) stack.push_back(child);
    }
    if (reachable.size() != tasks.size()) {
        errors.push_back("some tasks are unreachable from the root");
    }
}

} // namespace cli::ng
