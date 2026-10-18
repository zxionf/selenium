cmake_minimum_required(VERSION 3.20)
project(sele LANGUAGES CXX)

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

add_executable(sele
    src/main.cpp
    src/cli/new.cpp
    src/cli/append_decision.cpp
    src/cli/status.cpp
    src/cli/lint.cpp
    src/cli/close.cpp
    src/core/root.cpp
    src/core/id.cpp
    src/core/template.cpp
    src/core/git.cpp
    src/util/output.cpp
)

target_include_directories(sele PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(sele PRIVATE
    CLI11::CLI11
    nlohmann_json::nlohmann_json
)

set_target_properties(sele PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/build
)

enable_testing()
if(WIN32)
    add_test(NAME cli WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND powershell -ExecutionPolicy Bypass -File ${CMAKE_SOURCE_DIR}/tests/cli_tests.ps1 -SeleBin $<TARGET_FILE:sele>)
else()
    add_test(NAME cli WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} COMMAND bash tests/cli_tests.sh $<TARGET_FILE:sele>)
endif()