#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace core::sele {

using json = nlohmann::ordered_json;

struct Layout {
    std::filesystem::path root;
    std::filesystem::path sele;
    std::filesystem::path lines;
    std::filesystem::path epics;
    std::filesystem::path runtime;
    std::filesystem::path state;
    std::filesystem::path sessions;
    std::filesystem::path log;
    std::filesystem::path events;
};

struct Config {
    int schema_version = 1;
    std::string actor = "git";
    std::vector<std::string> modules_allow;
    std::string checkpoint_strategy = "git-commit";
    bool allow_empty = false;
    std::string commit_language = "en";
    std::string commit_scope = "sele";
    std::string commit_task_type = "auto";
};

std::optional<std::filesystem::path> find_root(const std::filesystem::path& start);
Layout layout(const std::filesystem::path& root);
void ensure_runtime(const Layout& layout);

Config load_config(const Layout& layout);
std::string resolve_actor(const Config& config);

std::string utc_now();
std::string utc_stamp();

bool valid_slug(const std::string& slug);
bool valid_line_id(const std::string& id);
bool valid_epic_id(const std::string& id);

std::filesystem::path line_dir(const Layout& layout, const std::string& id);
std::filesystem::path line_file(const Layout& layout, const std::string& id);
std::filesystem::path tasks_dir(const Layout& layout, const std::string& id);
std::filesystem::path state_file(const Layout& layout, const std::string& id);
std::filesystem::path epic_dir(const Layout& layout, const std::string& id);
std::filesystem::path epic_file(const Layout& layout, const std::string& id);

json read_json(const std::filesystem::path& path);
void write_json(const std::filesystem::path& path, const json& value);

} // namespace core::sele
