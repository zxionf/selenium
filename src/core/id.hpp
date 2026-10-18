#pragma once

#include <string>

namespace core {

bool validate_slug(const std::string& slug);
std::string generate_id(const std::string& slug);
bool parse_id(const std::string& id, std::string* date_out = nullptr, std::string* slug_out = nullptr);
bool validate_id(const std::string& id);

} // namespace core