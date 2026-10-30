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

std::string year_month(const std::string& ts) {
    if (ts.size() < 7) return "unknown";
    return ts.substr(0, 7);
}

} // namespace

Paths make_paths(const std::filesystem::path& root) {
    Paths paths;
    paths.root = root;
    paths.sele = root / ".sele";
    paths.plans = paths.sele / "plans";
    paths.lines = paths.sele / "lines";
    paths.tasks = paths.sele / "tasks";
    paths.checkpoints = paths.sele / "checkpoints";
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
    core::fsys::ensure_dir(paths.tasks);
    core::fsys::ensure_dir(paths.checkpoints);
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

std::filesystem::path task_path(const Paths& paths, const std::string& uid) {
    return paths.tasks / core::id::task_bucket(uid) / (core::id::task_file(uid) + ".json");
}

std::filesystem::path checkpoint_path(const Paths& paths, const std::string& ts,
                                      const std::string& id) {
    return paths.checkpoints / year_month(ts) / (id + ".json");
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

void save_task(const Paths& paths, const model::Task& task) {
    const auto path = task_path(paths, task.uid);
    core::fsys::ensure_dir(path.parent_path());
    core::fsys::write_atomic(path, model::to_json(task).dump(2) + "\n");
}

void save_checkpoint(const Paths& paths, const model::Checkpoint& checkpoint) {
    const auto path = checkpoint_path(paths, checkpoint.ts, checkpoint.id);
    core::fsys::ensure_dir(path.parent_path());
    core::fsys::write_atomic(path, model::to_json(checkpoint).dump(2) + "\n");
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
    if (!core::fsys::exists(paths.tasks)) return result;
    for (const auto& bucket : std::filesystem::directory_iterator(paths.tasks)) {
        if (!bucket.is_directory()) continue;
        for (const auto& entry : std::filesystem::directory_iterator(bucket.path())) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
            result.push_back(model::task_from_json(parse(entry.path())));
        }
    }
    std::sort(result.begin(), result.end(), [](const model::Task& left, const model::Task& right) {
        return left.uid < right.uid;
    });
    return result;
}

} // namespace core::store
