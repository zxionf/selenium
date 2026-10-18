#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core {

std::string execute_git(const std::filesystem::path& repo_path, const std::vector<std::string>& args);
std::string execute_git_silent(const std::filesystem::path& repo_path, const std::vector<std::string>& args);
bool is_git_repo(const std::filesystem::path& path);
bool has_head(const std::filesystem::path& path);
std::string diff_decisions_log(const std::filesystem::path& repo_path);

} // namespace core