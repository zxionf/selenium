#include "core/event.hpp"

#include "core/fs.hpp"

#include <nlohmann/json.hpp>

#include <sstream>
#include <stdexcept>

namespace core::event {

void append(const store::Paths& paths, const model::Event& event) {
    core::fsys::append_line(paths.log, model::to_json(event).dump());
}

std::vector<model::Event> load(const store::Paths& paths) {
    std::vector<model::Event> result;
    const auto content = core::fsys::read_file(paths.log);
    if (content.empty()) return result;

    std::istringstream stream(content);
    std::string line;
    std::size_t number = 0;
    while (std::getline(stream, line)) {
        ++number;
        if (line.empty()) continue;
        try {
            result.push_back(model::event_from_json(nlohmann::json::parse(line)));
        } catch (const std::exception& error) {
            throw std::runtime_error("Invalid event at log.jsonl:" + std::to_string(number) +
                                     ": " + error.what());
        }
    }
    return result;
}

} // namespace core::event
