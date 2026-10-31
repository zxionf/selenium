cmake_minimum_required(VERSION 3.20)
project(sele VERSION 0.21.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic -Werror")
elseif(MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4 /WX")
endif()

include(FetchContent)

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.4.2
    GIT_SHALLOW    ON
)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.2
    GIT_SHALLOW    ON
)

FetchContent_MakeAvailable(CLI11 nlohmann_json)

# CLI11 and nlohmann/json are header-only. By default they are compiled
# directly into the executable (no shared libraries). Set SELE_SHARED_DEPS=ON
# to instead wrap each into a shared library and link them dynamically.
option(SELE_SHARED_DEPS "Link CLI11 and nlohmann/json as shared libraries" OFF)

if(SELE_SHARED_DEPS)
    # Wrap each header-only dependency into a shared library that exports the
    # version symbols; the executable references these symbols (via --version)
    # and therefore depends on the shared libraries at runtime.
    add_library(cli11_shared SHARED src/thirdparty/cli11_shared.cpp)
    target_link_libraries(cli11_shared PUBLIC CLI11::CLI11)
    target_compile_definitions(cli11_shared PRIVATE SELE_DEP_BUILD)
    target_include_directories(cli11_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

    add_library(nlohmann_json_shared SHARED src/thirdparty/nlohmann_json_shared.cpp)
    target_link_libraries(nlohmann_json_shared PUBLIC nlohmann_json::nlohmann_json)
    target_compile_definitions(nlohmann_json_shared PRIVATE SELE_DEP_BUILD)
    target_include_directories(nlohmann_json_shared PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

add_executable(sele
    src/main.cpp
    src/cli/common.cpp
    src/cli/init.cpp
    src/cli/version.cpp
    src/cli/plan.cpp
    src/cli/line.cpp
    src/cli/task.cpp
    src/cli/step.cpp
    src/cli/query.cpp
    src/cli/git.cpp
    src/cli/cache.cpp
    src/cli/config.cpp
    src/cli/agent.cpp
    src/core/git.cpp
    src/core/config.cpp
    src/core/entity.cpp
    src/core/event.cpp
    src/core/fs.cpp
    src/core/hash.cpp
    src/core/id.cpp
    src/core/state.cpp
    src/core/store.cpp
    src/util/output.cpp
    src/util/color.cpp
)

# Single source of truth for the version: project(VERSION) above. The generated
# header is included as <sele_version.hpp>.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp.in
    ${CMAKE_BINARY_DIR}/generated/sele_version.hpp
    @ONLY
)

target_include_directories(sele PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

if(SELE_SHARED_DEPS)
    target_compile_definitions(sele PRIVATE SELE_DEPS_SHARED)
    target_link_libraries(sele PRIVATE cli11_shared nlohmann_json_shared)

    set_target_properties(sele cli11_shared nlohmann_json_shared PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build
        ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build
    )

    if(UNIX)
        set_target_properties(sele PROPERTIES
            BUILD_RPATH "$ORIGIN"
            INSTALL_RPATH "$ORIGIN"
        )
    endif()
else()
    target_link_libraries(sele PRIVATE CLI11::CLI11 nlohmann_json::nlohmann_json)

    set_target_properties(sele PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build
    )
endif()

# Installation: `cmake --install build --prefix <dir>` puts sele in <dir>/bin.
# With SELE_SHARED_DEPS=ON the wrapper libraries go to bin as well, so $ORIGIN
# (Linux) and same-directory DLL lookup (Windows) keep working.
install(TARGETS sele RUNTIME DESTINATION bin)
if(SELE_SHARED_DEPS)
    install(TARGETS cli11_shared nlohmann_json_shared
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION bin
    )
endif()

enable_testing()
if(NOT WIN32 AND EXISTS ${CMAKE_SOURCE_DIR}/tests/sele_v2_tests.sh)
    add_test(NAME sele_v2 WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND bash tests/sele_v2_tests.sh $<TARGET_FILE:sele>)
endif()