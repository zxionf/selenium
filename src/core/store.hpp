#pragma once

#include "core/entity.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace core::store {

struct Paths {
    std::filesystem::path root;
    std::filesystem::path sele;
    std::filesystem::path plans;
    std::filesystem::path lines;
    std::filesystem::path tasks;
    std::filesystem::path checkpoints;
    std::filesystem::path archive;
    std::filesystem::path cache;
    std::filesystem::path log;
    std::filesystem::path config;
    std::filesystem::path context;
};

Paths make_paths(const std::filesystem::path& root);
void ensure_layout(const Paths& paths);

std::filesystem::path plan_path(const Paths& paths, const std::string& id);
std::filesystem::path line_path(const Paths& paths, const std::string& plan,
                                const std::string& id);
std::filesystem::path task_path(const Paths& paths, const std::string& uid);
std::filesystem::path checkpoint_path(const Paths& paths, const std::string& ts,
                                      const std::string& id);

void save_plan(const Paths& paths, const model::Plan& plan);
void save_line(const Paths& paths, const model::Line& line);
void save_task(const Paths& paths, const model::Task& task);
void save_checkpoint(const Paths& paths, const model::Checkpoint& checkpoint);

std::vector<model::Plan> load_plans(const Paths& paths);
std::vector<model::Line> load_lines(const Paths& paths);
std::vector<model::Task> load_tasks(const Paths& paths);

} // namespace core::store
