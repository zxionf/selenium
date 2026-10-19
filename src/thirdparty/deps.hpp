#pragma once

#include <string>

#if defined(SELE_DEPS_SHARED)
#include "thirdparty/deps_shared.hpp"
#else
#include <CLI/Version.hpp>
#include <nlohmann/json.hpp>
#endif

namespace deps {

inline std::string cli11_version() {
#if defined(SELE_DEPS_SHARED)
    return sele_cli11_version();
#else
    return CLI11_VERSION;
#endif
}

inline std::string nlohmann_json_version() {
#if defined(SELE_DEPS_SHARED)
    return sele_nlohmann_json_version();
#else
    return std::to_string(NLOHMANN_JSON_VERSION_MAJOR) + "." +
           std::to_string(NLOHMANN_JSON_VERSION_MINOR) + "." +
           std::to_string(NLOHMANN_JSON_VERSION_PATCH);
#endif
}

} // namespace deps
