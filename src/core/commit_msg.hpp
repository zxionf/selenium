#pragma once

#include <string>
#include <utility>
#include <vector>

#include "core/sele.hpp"

namespace core::commit {

enum class Kind {
    Plan,
    TaskCheckpoint,
    Done,
    MarkMerged,
    UnmarkMerged,
    MergeCommit,
    RecordMerge,
    Rebase,
};

struct Context {
    Kind kind = Kind::Plan;
    std::string line_id;
    std::string line_type = "feature";
    std::string task_id;
    std::string task_title;
    std::vector<std::string> task_modules;

    Context() = default;
    Context(Kind context_kind, std::string id, std::string type,
            std::string task = {}, std::string title = {},
            std::vector<std::string> modules = {})
        : kind(context_kind),
          line_id(std::move(id)),
          line_type(std::move(type)),
          task_id(std::move(task)),
          task_title(std::move(title)),
          task_modules(std::move(modules)) {}
};

// Builds "<type>(<scope>): <subject>". The prefix is always English; the
// subject uses config.commit.language (or SELE_COMMIT_LANG when set).
std::string message(const Context& context, const sele::Config& config);

} // namespace core::commit
