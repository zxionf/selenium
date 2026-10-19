#include "thirdparty/deps_shared.hpp"

#include <CLI/CLI.hpp>

extern "C" SELE_DEP_API const char* sele_cli11_version() {
    return CLI11_VERSION;
}
