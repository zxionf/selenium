#include "core/journal.hpp"

#include <fstream>
#include <string>

#include "core/fs.hpp"

namespace core::journal {

void append(const std::filesystem::path& path, const nlohmann::ordered_json& event) {
    fsys::append_line(path, event.dump());
}

ReadResult read(const std::filesystem::path& path) {
    ReadResult result;
    std::ifstream in(path, std::ios::binary);
    if (!in) return result;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(in, line)) {
        ++line_number;
        if (line.empty()) continue;
        try {
            auto value = nlohmann::ordered_json::parse(line);
            if (value.is_object()) {
                result.events.push_back(std::move(value));
            } else {
                ++result.bad_lines;
                if (result.first_bad_line == 0) result.first_bad_line = line_number;
            }
        } catch (const nlohmann::json::parse_error&) {
            ++result.bad_lines;
            if (result.first_bad_line == 0) result.first_bad_line = line_number;
        }
    }
    return result;
}

} // namespace core::journal
