#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

#include <nlohmann/json.hpp>

namespace core::journal {

struct ReadResult {
    std::vector<nlohmann::ordered_json> events;
    std::size_t bad_lines = 0;
    std::size_t first_bad_line = 0;
};

void append(const std::filesystem::path& path, const nlohmann::ordered_json& event);
ReadResult read(const std::filesystem::path& path);

} // namespace core::journal
