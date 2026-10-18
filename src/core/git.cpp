#include "core/git.hpp"
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace core {

std::string execute_git(const std::filesystem::path&, const std::vector<std::string>& args) {
    std::string cmd = "git";
    for (const auto& arg : args) {
        cmd += " " + arg;
    }

    FILE* pipe = nullptr;
#ifdef _WIN32
    pipe = _popen(cmd.c_str(), "r");
#else
    pipe = popen(cmd.c_str(), "r");
#endif

    if (!pipe) {
        throw std::runtime_error("Failed to execute git command: " + cmd);
    }

    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

#ifdef _WIN32
    int status = _pclose(pipe);
#else
    int status = pclose(pipe);
#endif

    if (status != 0) {
        throw std::runtime_error("Git command failed: " + cmd + " (exit code: " + std::to_string(status) + ")");
    }

    return result;
}

std::string execute_git_silent(const std::filesystem::path&, const std::vector<std::string>& args) {
    std::string cmd = "git";
    for (const auto& arg : args) {
        cmd += " " + arg;
    }

#ifdef _WIN32
    cmd += " 2>nul";
#else
    cmd += " 2>/dev/null";
#endif

    FILE* pipe = nullptr;
#ifdef _WIN32
    pipe = _popen(cmd.c_str(), "r");
#else
    pipe = popen(cmd.c_str(), "r");
#endif

    if (!pipe) {
        return "";
    }

    std::string result;
    char buffer[128];
    while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result += buffer;
    }

#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif

    return result;
}

bool is_git_repo(const std::filesystem::path& path) {
    std::string result = execute_git_silent(path, {"rev-parse", "--git-dir"});
    return !result.empty();
}

bool has_head(const std::filesystem::path& path) {
    std::string result = execute_git_silent(path, {"rev-parse", "--verify", "HEAD"});
    return !result.empty();
}

std::string diff_decisions_log(const std::filesystem::path& repo_path) {
    return execute_git(repo_path, {"diff", "--no-color", "--", ".design/DECISIONS.log"});
}

} // namespace core