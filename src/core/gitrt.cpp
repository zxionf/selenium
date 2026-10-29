#include "core/gitrt.hpp"

#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <cstdio>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace core::gitrt {

namespace {

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && (value[begin] == ' ' || value[begin] == '\n' || value[begin] == '\r' ||
                           value[begin] == '\t')) {
        ++begin;
    }
    while (end > begin && (value[end - 1] == ' ' || value[end - 1] == '\n' ||
                           value[end - 1] == '\r' || value[end - 1] == '\t')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

#ifdef _WIN32
std::string quote(const std::string& arg) {
    std::string out = "\"";
    for (char ch : arg) {
        if (ch == '"') out += "\\\"";
        else out += ch;
    }
    out += "\"";
    return out;
}
#endif

} // namespace

Result run(const std::filesystem::path& cwd, const std::vector<std::string>& args) {
    std::vector<std::string> full;
    full.reserve(args.size() + 1);
    full.push_back("git");
    full.insert(full.end(), args.begin(), args.end());

#ifdef _WIN32
    std::string command;
    for (const auto& part : full) {
        if (!command.empty()) command += ' ';
        command += quote(part);
    }
    command += " 2>&1";
    FILE* pipe = _popen(command.c_str(), "r");
    if (!pipe) throw std::runtime_error("Failed to run git");
    std::string out;
    char buffer[512];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) out += buffer;
    const int status = _pclose(pipe);
    return {status, out};
#else
    std::vector<char*> argv;
    argv.reserve(full.size() + 1);
    for (auto& part : full) argv.push_back(const_cast<char*>(part.c_str()));
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) throw std::runtime_error("Failed to create pipe for git");

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]);
        close(fds[1]);
        throw std::runtime_error("Failed to fork for git");
    }
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], STDOUT_FILENO) < 0) _exit(127);
        if (dup2(fds[1], STDERR_FILENO) < 0) _exit(127);
        close(fds[1]);
        if (chdir(cwd.c_str()) != 0) _exit(127);
        execvp("git", argv.data());
        _exit(127);
    }

    close(fds[1]);
    std::string out;
    char buffer[4096];
    ssize_t count = 0;
    while ((count = read(fds[0], buffer, sizeof(buffer))) > 0) {
        out.append(buffer, static_cast<std::size_t>(count));
    }
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const int code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    return {code, out};
#endif
}

bool is_repo(const std::filesystem::path& cwd) {
    const Result result = run(cwd, {"rev-parse", "--git-dir"});
    return result.code == 0 && !trim(result.out).empty();
}

bool has_head(const std::filesystem::path& cwd) {
    const Result result = run(cwd, {"rev-parse", "--verify", "--quiet", "HEAD"});
    return result.code == 0;
}

bool is_dirty(const std::filesystem::path& cwd) {
    const Result result = run(cwd, {"status", "--porcelain", "--untracked-files=no", "--", ".",
                                    ":(exclude).sele"});
    if (result.code != 0) {
        throw std::runtime_error("git status failed: " + trim(result.out));
    }
    return !trim(result.out).empty();
}

std::string current_branch(const std::filesystem::path& cwd) {
    const Result result = run(cwd, {"rev-parse", "--abbrev-ref", "HEAD"});
    if (result.code != 0) return {};
    return trim(result.out);
}

bool branch_exists(const std::filesystem::path& cwd, const std::string& branch) {
    const Result result = run(cwd, {"rev-parse", "--verify", "--quiet", "refs/heads/" + branch});
    return result.code == 0;
}

void create_branch(const std::filesystem::path& cwd, const std::string& branch) {
    const Result result = run(cwd, {"switch", "-c", branch});
    if (result.code != 0) {
        throw std::runtime_error("git switch -c " + branch + " failed: " + trim(result.out));
    }
}

void switch_branch(const std::filesystem::path& cwd, const std::string& branch) {
    const Result result = run(cwd, {"switch", branch});
    if (result.code != 0) {
        throw std::runtime_error("git switch " + branch + " failed: " + trim(result.out));
    }
}

bool commit_all(const std::filesystem::path& cwd, const std::string& message) {
    const Result add = run(cwd, {"add", "-A"});
    if (add.code != 0) throw std::runtime_error("git add failed: " + trim(add.out));

    const Result status = run(cwd, {"status", "--porcelain"});
    if (status.code != 0) throw std::runtime_error("git status failed: " + trim(status.out));
    if (trim(status.out).empty()) return false;

    const Result commit = run(cwd, {"commit", "-m", message});
    if (commit.code != 0) throw std::runtime_error("git commit failed: " + trim(commit.out));
    return true;
}

std::string head_short(const std::filesystem::path& cwd) {
    const Result result = run(cwd, {"rev-parse", "--short", "HEAD"});
    if (result.code != 0) return {};
    return trim(result.out);
}

} // namespace core::gitrt
