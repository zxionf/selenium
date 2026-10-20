#include "core/version.hpp"

#include <string>

#include "sele_version.hpp"
#include "thirdparty/deps.hpp"

namespace core {

std::string version_string() {
    return SELE_VERSION_STRING;
}

std::string version_display() {
    std::string out = "sele " + version_string() + " (CLI11 ";
    out += deps::cli11_version();
    out += ", nlohmann/json ";
    out += deps::nlohmann_json_version();
    out += ")";
    return out;
}

} // namespace core
