#pragma once

#include "core/store.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace core::git {

struct Result {
    int code{0};
    std::string output;
};

bool present(const std::filesystem::path& root);
Result run(const std::filesystem::path& root, const std::vector<std::string>& args);

std::string head(const store::Paths& paths);
bool is_ancestor(const store::Paths& paths, const std::string& older, const std::string& newer);
std::string tree(const store::Paths& paths);
std::string branch(const store::Paths& paths);
std::vector<std::string> files_changed(const store::Paths& paths);
std::string diff_stat(const store::Paths& paths);

enum class DriftKind { NONE, D1, D2, D3, D6 };

struct Drift {
    DriftKind kind{DriftKind::NONE};
    std::string old_head;
    std::string new_head;
};

Drift detect_drift(const store::Paths& paths);
std::string kind_name(DriftKind kind);

} // namespace core::git
