#include "core/id.hpp"
#include <chrono>
#include <regex>
#include <sstream>
#include <iomanip>

namespace core {

bool validate_slug(const std::string& slug) {
    static const std::regex pattern("^[a-z0-9-]+$");
    return std::regex_match(slug, pattern);
}

std::string generate_id(const std::string& slug) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d") << '-' << slug;
    return oss.str();
}

bool parse_id(const std::string& id, std::string* date_out, std::string* slug_out) {
    static const std::regex pattern("^(\\d{8})-(.+)$");
    std::smatch match;
    if (!std::regex_match(id, match, pattern)) {
        return false;
    }
    if (date_out) *date_out = match[1].str();
    if (slug_out) *slug_out = match[2].str();
    return true;
}

bool validate_id(const std::string& id) {
    std::string date, slug;
    if (!parse_id(id, &date, &slug)) {
        return false;
    }
    return validate_slug(slug);
}

} // namespace core