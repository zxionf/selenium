#include "core/commit_msg.hpp"

#include <cstdlib>

namespace core::commit {

namespace {

std::string language(const sele::Config& config) {
    if (const char* env = std::getenv("SELE_COMMIT_LANG"); env != nullptr && env[0] != '\0') {
        return env;
    }
    return config.commit_language;
}

std::string scope_for(const Context& context, const sele::Config& config) {
    if (config.commit_scope == "module") {
        if (context.kind == Kind::TaskCheckpoint && !context.task_modules.empty()) {
            return context.task_modules.front();
        }
        return "sele";
    }
    if (config.commit_scope == "sele") return "sele";
    if (!config.commit_scope.empty()) return config.commit_scope;
    return "sele";
}

std::string type_for(const Context& context, const sele::Config& config) {
    if (context.kind != Kind::TaskCheckpoint) return "chore";
    if (config.commit_task_type != "auto") return config.commit_task_type;
    if (context.line_type == "feature") return "feat";
    if (context.line_type == "fix") return "fix";
    if (context.line_type == "refactor") return "refactor";
    return "chore";
}

std::string subject(const Context& context, bool chinese) {
    const std::string& id = context.line_id;
    switch (context.kind) {
    case Kind::Plan:
        return chinese ? "规划 " + id : "plan " + id;
    case Kind::TaskCheckpoint:
        return context.task_id + " " + context.task_title;
    case Kind::Done:
        return chinese ? "完成线 " + id : "line " + id + " done";
    case Kind::MarkMerged:
        return chinese ? "标记 " + id + " 为已合并" : "mark " + id + " merged";
    case Kind::UnmarkMerged:
        return chinese ? "撤销 " + id + " 的已合并标记" : "unmark " + id + " merged";
    case Kind::MergeCommit:
        return chinese ? "合并 " + id : "merge " + id;
    case Kind::RecordMerge:
        return chinese ? "记录 " + id + " 的合并提交" : "record merge " + id;
    case Kind::Rebase:
        return chinese ? "变基 " + id : "rebase " + id;
    }
    return id;
}

} // namespace

std::string message(const Context& context, const sele::Config& config) {
    const bool chinese = language(config) == "zh";
    return type_for(context, config) + "(" + scope_for(context, config) + "): " +
           subject(context, chinese);
}

} // namespace core::commit
