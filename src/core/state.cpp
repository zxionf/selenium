#include "core/state.hpp"

#include <initializer_list>
#include <utility>

namespace core::state {

namespace {

bool allowed(std::initializer_list<std::pair<const char*, const char*>> edges,
             const std::string& from, const std::string& to) {
    for (const auto& edge : edges) {
        if (from == edge.first && to == edge.second) return true;
    }
    return false;
}

} // namespace

bool plan_transition(const std::string& from, const std::string& to) {
    return allowed({{"draft", "pending_approval"},
                    {"pending_approval", "active"},
                    {"pending_approval", "draft"},
                    {"active", "done"},
                    {"active", "aborted"},
                    {"done", "archived"},
                    {"archived", "active"}},
                   from, to);
}

bool line_transition(const std::string& from, const std::string& to) {
    return allowed({{"draft", "active"},
                    {"active", "done"},
                    {"active", "blocked"},
                    {"blocked", "active"},
                    {"draft", "abandoned"},
                    {"active", "abandoned"},
                    {"blocked", "abandoned"}},
                   from, to);
}

bool task_transition(const std::string& from, const std::string& to) {
    return allowed({{"todo", "in_progress"},
                    {"in_progress", "done"},
                    {"in_progress", "pending_review"},
                    {"in_progress", "paused"},
                    {"in_progress", "blocked"},
                    {"in_progress", "failed"},
                    {"pending_review", "done"},
                    {"pending_review", "in_progress"},
                    {"paused", "in_progress"},
                    {"blocked", "in_progress"},
                    {"failed", "in_progress"},
                    {"done", "archived"},
                    {"archived", "todo"}},
                   from, to);
}

bool step_transition(const std::string& from, const std::string& to) {
    return allowed({{"todo", "in_progress"},
                    {"todo", "skipped"},
                    {"in_progress", "done"},
                    {"in_progress", "pending_review"},
                    {"in_progress", "blocked"},
                    {"in_progress", "failed"},
                    {"in_progress", "skipped"},
                    {"pending_review", "done"},
                    {"pending_review", "in_progress"},
                    {"blocked", "in_progress"},
                    {"failed", "in_progress"}},
                   from, to);
}

} // namespace core::state
