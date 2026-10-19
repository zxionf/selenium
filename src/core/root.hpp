#pragma once

#include <filesystem>
#include <string>

namespace core {

bool is_initialized(const std::filesystem::path& start);
std::filesystem::path find_design_root(const std::filesystem::path& start);
std::filesystem::path find_agent_root(const std::filesystem::path& start);

} // namespace core