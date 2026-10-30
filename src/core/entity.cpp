#include "core/entity.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace core::model {

namespace {

std::string string_or(const nlohmann::json& value, const char* key, const std::string& fallback) {
    const auto found = value.find(key);
    if (found == value.end() || !found->is_string()) return fallback;
    return found->get<std::string>();
}

bool bool_or(const nlohmann::json& value, const char* key, bool fallback) {
    const auto found = value.find(key);
    if (found == value.end() || !found->is_boolean()) return fallback;
    return found->get<bool>();
}

} // namespace

std::string now_utc() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream text;
    text << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return text.str();
}

nlohmann::json to_json(const Acceptance& value) {
    return {{"id", value.id},
            {"desc", value.desc},
            {"status", value.status},
            {"verified_by", value.verified_by.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(value.verified_by)},
            {"verified_at", value.verified_at.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(value.verified_at)},
            {"requires_human", value.requires_human}};
}

nlohmann::json to_json(const Step& value) {
    return {{"id", value.id},
            {"title", value.title},
            {"desc", value.desc},
            {"status", value.status},
            {"checkpoint", value.checkpoint.empty() ? nlohmann::json(nullptr)
                                                    : nlohmann::json(value.checkpoint)},
            {"skipped_reason", value.skipped_reason.empty() ? nlohmann::json(nullptr)
                                                            : nlohmann::json(value.skipped_reason)},
            {"jump_meta", value.jump_meta.empty() ? nlohmann::json(nullptr)
                                                  : nlohmann::json(value.jump_meta)},
            {"requires_review", value.requires_review}};
}

nlohmann::json to_json(const Task& value) {
    nlohmann::json acceptance = nlohmann::json::array();
    for (const auto& item : value.acceptance) acceptance.push_back(to_json(item));
    nlohmann::json steps = nlohmann::json::array();
    for (const auto& item : value.steps) steps.push_back(to_json(item));
    return {{"uid", value.uid},
            {"id", value.id},
            {"plan", value.plan},
            {"line", value.line},
            {"title", value.title},
            {"goal", value.goal},
            {"status", value.status},
            {"review_policy", value.review_policy},
            {"current_step", value.current_step.empty() ? nlohmann::json(nullptr)
                                                        : nlohmann::json(value.current_step)},
            {"checkpoint_strategy", value.checkpoint_strategy},
            {"modules", value.modules},
            {"depends", value.depends},
            {"acceptance", acceptance},
            {"steps", steps},
            {"pause_context", value.pause_context},
            {"locked", value.locked},
            {"archived_at", value.archived_at.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(value.archived_at)},
            {"archived_git_head", value.archived_git_head.empty()
                                      ? nlohmann::json(nullptr)
                                      : nlohmann::json(value.archived_git_head)},
            {"created_at", value.created_at},
            {"finished_at", value.finished_at.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(value.finished_at)}};
}

nlohmann::json to_json(const Line& value) {
    nlohmann::json acceptance = nlohmann::json::array();
    for (const auto& item : value.acceptance) acceptance.push_back(to_json(item));
    return {{"id", value.id},
            {"plan", value.plan},
            {"goal", value.goal},
            {"status", value.status},
            {"created_at", value.created_at},
            {"modules", value.modules},
            {"depends", value.depends},
            {"acceptance", acceptance}};
}

nlohmann::json to_json(const Plan& value) {
    nlohmann::json acceptance = nlohmann::json::array();
    for (const auto& item : value.acceptance) acceptance.push_back(to_json(item));
    nlohmann::json lines = nlohmann::json::array();
    for (const auto& item : value.lines) {
        lines.push_back({{"id", item.id}, {"depends", item.depends}});
    }
    return {{"id", value.id},
            {"goal", value.goal},
            {"status", value.status},
            {"created_at", value.created_at},
            {"approved_by", value.approved_by.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(value.approved_by)},
            {"approved_at", value.approved_at.empty() ? nlohmann::json(nullptr)
                                                      : nlohmann::json(value.approved_at)},
            {"acceptance", acceptance},
            {"lines", lines}};
}

nlohmann::json to_json(const Checkpoint& value) {
    return {{"id", value.id},
            {"ts", value.ts},
            {"status", value.status},
            {"entity", value.entity},
            {"git", value.git}};
}

nlohmann::json to_json(const Event& value) {
    nlohmann::json result = {{"ts", value.ts},
                             {"actor", value.actor},
                             {"op", value.op},
                             {"entity", value.entity},
                             {"before", value.before},
                             {"after", value.after},
                             {"context", value.context}};
    if (!value.reason.empty()) result["reason"] = value.reason;
    if (!value.checkpoint.empty()) result["checkpoint"] = value.checkpoint;
    return result;
}

Acceptance acceptance_from_json(const nlohmann::json& value) {
    Acceptance result;
    result.id = string_or(value, "id", "");
    result.desc = string_or(value, "desc", "");
    result.status = string_or(value, "status", "unverified");
    result.verified_by = string_or(value, "verified_by", "");
    result.verified_at = string_or(value, "verified_at", "");
    result.requires_human = bool_or(value, "requires_human", false);
    return result;
}

Step step_from_json(const nlohmann::json& value) {
    Step result;
    result.id = string_or(value, "id", "");
    result.title = string_or(value, "title", "");
    result.desc = string_or(value, "desc", "");
    result.status = string_or(value, "status", "todo");
    result.checkpoint = string_or(value, "checkpoint", "");
    result.skipped_reason = string_or(value, "skipped_reason", "");
    result.jump_meta = string_or(value, "jump_meta", "");
    result.requires_review = bool_or(value, "requires_review", false);
    return result;
}

Task task_from_json(const nlohmann::json& value) {
    Task result;
    result.uid = string_or(value, "uid", "");
    result.id = string_or(value, "id", "");
    result.plan = string_or(value, "plan", "");
    result.line = string_or(value, "line", "");
    result.title = string_or(value, "title", "");
    result.goal = string_or(value, "goal", "");
    result.status = string_or(value, "status", "todo");
    result.review_policy = string_or(value, "review_policy", "mixed");
    result.current_step = string_or(value, "current_step", "");
    result.checkpoint_strategy = string_or(value, "checkpoint_strategy", "per-step");
    if (const auto found = value.find("modules"); found != value.end() && found->is_array()) {
        result.modules = found->get<std::vector<std::string>>();
    }
    if (const auto found = value.find("depends"); found != value.end()) result.depends = *found;
    if (const auto found = value.find("acceptance"); found != value.end() && found->is_array()) {
        for (const auto& item : *found) result.acceptance.push_back(acceptance_from_json(item));
    }
    if (const auto found = value.find("steps"); found != value.end() && found->is_array()) {
        for (const auto& item : *found) result.steps.push_back(step_from_json(item));
    }
    if (const auto found = value.find("pause_context"); found != value.end()) {
        result.pause_context = *found;
    }
    if (const auto found = value.find("locked"); found != value.end()) result.locked = *found;
    result.archived_at = string_or(value, "archived_at", "");
    result.archived_git_head = string_or(value, "archived_git_head", "");
    result.created_at = string_or(value, "created_at", "");
    result.finished_at = string_or(value, "finished_at", "");
    return result;
}

Line line_from_json(const nlohmann::json& value) {
    Line result;
    result.id = string_or(value, "id", "");
    result.plan = string_or(value, "plan", "");
    result.goal = string_or(value, "goal", "");
    result.status = string_or(value, "status", "draft");
    result.created_at = string_or(value, "created_at", "");
    if (const auto found = value.find("modules"); found != value.end() && found->is_array()) {
        result.modules = found->get<std::vector<std::string>>();
    }
    if (const auto found = value.find("depends"); found != value.end()) result.depends = *found;
    if (const auto found = value.find("acceptance"); found != value.end() && found->is_array()) {
        for (const auto& item : *found) result.acceptance.push_back(acceptance_from_json(item));
    }
    return result;
}

Plan plan_from_json(const nlohmann::json& value) {
    Plan result;
    result.id = string_or(value, "id", "");
    result.goal = string_or(value, "goal", "");
    result.status = string_or(value, "status", "draft");
    result.created_at = string_or(value, "created_at", "");
    result.approved_by = string_or(value, "approved_by", "");
    result.approved_at = string_or(value, "approved_at", "");
    if (const auto found = value.find("acceptance"); found != value.end() && found->is_array()) {
        for (const auto& item : *found) result.acceptance.push_back(acceptance_from_json(item));
    }
    if (const auto found = value.find("lines"); found != value.end() && found->is_array()) {
        for (const auto& item : *found) {
            LineRef ref;
            ref.id = string_or(item, "id", "");
            if (const auto depends = item.find("depends"); depends != item.end()) ref.depends = *depends;
            result.lines.push_back(ref);
        }
    }
    return result;
}

Checkpoint checkpoint_from_json(const nlohmann::json& value) {
    Checkpoint result;
    result.id = string_or(value, "id", "");
    result.ts = string_or(value, "ts", "");
    result.status = string_or(value, "status", "valid");
    if (const auto found = value.find("entity"); found != value.end()) result.entity = *found;
    if (const auto found = value.find("git"); found != value.end()) result.git = *found;
    return result;
}

Event event_from_json(const nlohmann::json& value) {
    Event result;
    result.ts = string_or(value, "ts", "");
    result.actor = string_or(value, "actor", "");
    result.op = string_or(value, "op", "");
    result.reason = string_or(value, "reason", "");
    result.checkpoint = string_or(value, "checkpoint", "");
    if (const auto found = value.find("entity"); found != value.end()) result.entity = *found;
    if (const auto found = value.find("before"); found != value.end()) result.before = *found;
    if (const auto found = value.find("after"); found != value.end()) result.after = *found;
    if (const auto found = value.find("context"); found != value.end()) result.context = *found;
    return result;
}

} // namespace core::model
