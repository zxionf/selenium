#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace core::gitrt {

struct Result {
    int code = -1;
    std::string out;
};

// Run git with an argv array (no shell). cwd is the directory git runs in.
Result run(const std::filesystem::path& cwd, const std::vector<std::string>& args);

bool is_repo(const std::filesystem::path& cwd);
bool has_head(const std::filesystem::path& cwd);

// True when tracked code differs from HEAD. `.sele/` is excluded on purpose:
// sele writes its own metadata there while the tree may be otherwise clean.
bool is_dirty(const std::filesystem::path& cwd);

std::string current_branch(const std::filesystem::path& cwd);
bool branch_exists(const std::filesystem::path& cwd, const std::string& branch);
void create_branch(const std::filesystem::path& cwd, const std::string& branch);
void switch_branch(const std::filesystem::path& cwd, const std::string& branch);

// `git add -A` + commit. Returns false when there was nothing to commit.
bool commit_all(const std::filesystem::path& cwd, const std::string& message);

std::string head_short(const std::filesystem::path& cwd);

} // namespace core::gitrt
