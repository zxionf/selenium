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
    std::filesystem::path archive;
    std::filesystem::path cache;
    std::filesystem::path log;
    std::filesystem::path config;
    std::filesystem::path context;
};

Paths make_paths(const std::filesystem::path& root);
void ensure_layout(const Paths& paths);

// Throws when .sele looks like a pre-0.20 layout. Never migrate, never read it.
void check_compatible(const Paths& paths);

std::filesystem::path plan_path(const Paths& paths, const std::string& id);
std::filesystem::path line_path(const Paths& paths, const std::string& plan,
                                const std::string& id);

void save_plan(const Paths& paths, const model::Plan& plan);
void save_line(const Paths& paths, const model::Line& line);
void save_task(const Paths& paths, const model::Task& task);

std::vector<model::Plan> load_plans(const Paths& paths);
std::vector<model::Line> load_lines(const Paths& paths);
std::vector<model::Task> load_tasks(const Paths& paths);

} // namespace core::store
