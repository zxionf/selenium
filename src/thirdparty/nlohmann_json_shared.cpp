#include "thirdparty/deps_shared.hpp"

#include <nlohmann/json.hpp>

#include <string>

extern "C" SELE_DEP_API const char* sele_nlohmann_json_version() {
    static const std::string version =
        std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." +
        std::to_string(NLOHMANN_JSON_VERSION_MINOR) + "." +
        std::to_string(NLOHMANN_JSON_VERSION_PATCH);
    return version.c_str();
}
