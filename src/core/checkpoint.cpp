#include "core/checkpoint.hpp"

#include "core/fs.hpp"
#include "core/git.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>

namespace core::checkpoint {

namespace {

std::string date_part(const std::string& ts) {
    if (ts.size() < 10) return "00000000";
    return ts.substr(0, 4) + ts.substr(5, 2) + ts.substr(8, 2);
}

} // namespace

std::string next_id(const store::Paths& paths, const std::string& ts) {
    const std::string date = date_part(ts);
    const auto directory = paths.checkpoints / ts.substr(0, 7);
    int highest = 0;
    if (core::fsys::exists(directory)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            const auto name = entry.path().stem().string();
            const std::string prefix = "ck-" + date + "-";
            if (name.rfind(prefix, 0) != 0) continue;
            try {
                highest = std::max(highest, std::stoi(name.substr(prefix.size())));
            } catch (...) {
            }
        }
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%03d", highest + 1);
    return "ck-" + date + "-" + buffer;
}

model::Checkpoint create(const store::Paths& paths, const model::Task& task,
                         const model::Step& step) {
    model::Checkpoint checkpoint;
    checkpoint.ts = model::now_utc();
    checkpoint.id = next_id(paths, checkpoint.ts);
    checkpoint.status = "valid";
    checkpoint.entity = {{"type", "step"},
                         {"plan", task.plan},
                         {"line", task.line},
                         {"task", task.id},
                         {"step", step.id},
                         {"task_uid", task.uid}};
    checkpoint.git = {{"commit", core::git::head(paths)},
                      {"tree", core::git::tree(paths)},
                      {"branch", core::git::branch(paths)},
                      {"dirty", false},
                      {"files_changed", core::git::files_changed(paths)},
                      {"diff_stat", core::git::diff_stat(paths)},
                      {"source", "sele"}};
    store::save_checkpoint(paths, checkpoint);
    return checkpoint;
}

model::Checkpoint create_task(const store::Paths& paths, const model::Task& task) {
    model::Checkpoint checkpoint;
    checkpoint.ts = model::now_utc();
    checkpoint.id = next_id(paths, checkpoint.ts);
    checkpoint.status = "valid";
    checkpoint.entity = {{"type", "task"},
                         {"plan", task.plan},
                         {"line", task.line},
                         {"task", task.id},
                         {"task_uid", task.uid}};
    checkpoint.git = {{"commit", core::git::head(paths)},
                      {"tree", core::git::tree(paths)},
                      {"branch", core::git::branch(paths)},
                      {"dirty", false},
                      {"files_changed", core::git::files_changed(paths)},
                      {"diff_stat", core::git::diff_stat(paths)},
                      {"source", "sele"}};
    store::save_checkpoint(paths, checkpoint);
    return checkpoint;
}

} // namespace core::checkpoint
