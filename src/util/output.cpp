#include "util/output.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

namespace util {

void Output::print_text(const std::string& message) {
    if (format_ == Format::TEXT) {
        std::cout << message << '\n';
    }
}

void Output::print_json(const nlohmann::json& json) {
    if (format_ == Format::JSON) {
        std::cout << json.dump() << '\n';
    }
}

void Output::print_both(const std::string& text, const nlohmann::json& json) {
    print_text(text);
    print_json(json);
}

void Output::error(const std::string& message, const std::string& suggestion,
                   const std::vector<std::string>& next) {
    if (format_ == Format::JSON) {
        nlohmann::json err;
        err["error"] = message;
        if (!suggestion.empty()) {
            err["suggestion"] = suggestion;
        }
        if (!next.empty()) {
            err["next"] = next;
        }
        std::cerr << err.dump() << '\n';
    } else {
        std::cerr << "Error: " << message << '\n';
        if (!suggestion.empty()) {
            std::cerr << "Suggestion: " << suggestion << '\n';
        }
        if (!next.empty()) {
            std::cerr << "Next:\n";
            for (const auto& command : next) {
                std::cerr << "  " << command << '\n';
            }
        }
    }
}

} // namespace util