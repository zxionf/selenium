#include "core/store.hpp"

#include "core/fs.hpp"
#include "core/id.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>

namespace core::store {

namespace {

nlohmann::json parse(const std::filesystem::path& path) {
    const auto content = core::fsys::read_file(path);
    if (content.empty()) throw std::runtime_error("Empty JSON file: " + path.string());
    try {
        return nlohmann::json::parse(content);
    } catch (const std::exception& error) {
        throw std::runtime_error("Invalid JSON in " + path.string() + ": " + error.what());
    }
}

} // namespace

Paths make_paths(const std::filesystem::path& root) {
    Paths paths;
    paths.root = root;
    paths.sele = root / ".sele";
    paths.plans = paths.sele / "plans";
    paths.lines = paths.sele / "lines";
    paths.archive = paths.sele / "archive";
    paths.cache = paths.sele / "cache";
    paths.log = paths.sele / "log.jsonl";
    paths.config = paths.sele / "config.json";
    paths.context = paths.cache / "context.json";
    return paths;
}

void ensure_layout(const Paths& paths) {
    core::fsys::ensure_dir(paths.plans);
    core::fsys::ensure_dir(paths.lines);
    core::fsys::ensure_dir(paths.archive);
    core::fsys::ensure_dir(paths.cache);
    if (!core::fsys::exists(paths.config)) {
        core::fsys::write_atomic(paths.config,
                                 nlohmann::json({{"version", 2},
                                                 {"created_at", model::now_utc()}})
                                     .dump(2) +
                                     "\n");
    }
    if (!core::fsys::exists(paths.log)) core::fsys::write_atomic(paths.log, "");
    if (!core::fsys::exists(paths.context)) {
        core::fsys::write_atomic(paths.context, nlohmann::json::object().dump(2) + "\n");
    }
}

std::filesystem::path plan_path(const Paths& paths, const std::string& id) {
    return paths.plans / (id + ".json");
}

std::filesystem::path line_path(const Paths& paths, const std::string& plan,
                                const std::string& id) {
    return paths.lines / (plan + "." + id + ".json");
}

void save_task(const Paths& paths, const model::Task& task) {
    const auto path = line_path(paths, task.plan, task.line);
    model::Line line;
    try {
        line = model::line_from_json(parse(path));
    } catch (const std::exception&) {
        throw std::runtime_error("Line not found for task " + task.id + ": " + path.string());
    }
    bool found = false;
    for (auto& existing : line.tasks) {
        if (existing.uid != task.uid) continue;
        existing = task;
        found = true;
    }
    if (!found) line.tasks.push_back(task);
    save_line(paths, line);
}
void save_plan(const Paths& paths, const model::Plan& plan) {
    core::fsys::ensure_dir(paths.plans);
    core::fsys::write_atomic(plan_path(paths, plan.id), model::to_json(plan).dump(2) + "\n");
}

void save_line(const Paths& paths, const model::Line& line) {
    core::fsys::ensure_dir(paths.lines);
    core::fsys::write_atomic(line_path(paths, line.plan, line.id),
                             model::to_json(line).dump(2) + "\n");
}


std::vector<model::Plan> load_plans(const Paths& paths) {
    std::vector<model::Plan> result;
    if (!core::fsys::exists(paths.plans)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(paths.plans)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        result.push_back(model::plan_from_json(parse(entry.path())));
    }
    std::sort(result.begin(), result.end(),
              [](const model::Plan& left, const model::Plan& right) { return left.id < right.id; });
    return result;
}

std::vector<model::Line> load_lines(const Paths& paths) {
    std::vector<model::Line> result;
    if (!core::fsys::exists(paths.lines)) return result;
    for (const auto& entry : std::filesystem::directory_iterator(paths.lines)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        result.push_back(model::line_from_json(parse(entry.path())));
    }
    std::sort(result.begin(), result.end(),
              [](const model::Line& left, const model::Line& right) {
                  return left.plan == right.plan ? left.id < right.id : left.plan < right.plan;
              });
    return result;
}

std::vector<model::Task> load_tasks(const Paths& paths) {
    std::vector<model::Task> result;
    for (const auto& line : load_lines(paths)) {
        for (const auto& task : line.tasks) result.push_back(task);
    }
    std::sort(result.begin(), result.end(), [](const model::Task& left, const model::Task& right) {
        return left.uid < right.uid;
    });
    return result;
}

} // namespace core::store
