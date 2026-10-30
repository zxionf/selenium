#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace core::model {

struct Acceptance {
    std::string id;
    std::string desc;
    std::string status{"unverified"}; // unverified | pass | fail
    std::string verified_by;
    std::string verified_at;
    bool requires_human{false};
};

struct Step {
    std::string id;
    std::string title;
    std::string desc;
    std::string status{"todo"}; // todo | in_progress | done | pending_review | blocked | failed | skipped
    std::string checkpoint;
    std::string skipped_reason;
    std::string jump_meta;
    bool requires_review{false};
};

struct Task {
    std::string uid;
    std::string id;
    std::string plan;
    std::string line;
    std::string title;
    std::string goal;
    std::string status{"todo"}; // todo | in_progress | done | pending_review | paused | blocked | failed | archived
    std::string review_policy{"mixed"};
    std::string current_step;
    std::string checkpoint_strategy{"per-step"};
    std::vector<std::string> modules;
    nlohmann::json depends = nlohmann::json::array();
    std::vector<Acceptance> acceptance;
    std::vector<Step> steps;
    nlohmann::json pause_context = nullptr;
    nlohmann::json locked = nlohmann::json::object({{"locked", false}});
    std::string archived_at;
    std::string archived_git_head;
    std::string created_at;
    std::string finished_at;
};

struct Line {
    std::string id;
    std::string plan;
    std::string goal;
    std::string status{"draft"}; // draft | active | done | blocked | abandoned
    std::string created_at;
    std::vector<std::string> modules;
    nlohmann::json depends = nlohmann::json::array();
    std::vector<Acceptance> acceptance;
};

struct LineRef {
    std::string id;
    nlohmann::json depends = nlohmann::json::array();
};

struct Plan {
    std::string id;
    std::string goal;
    std::string status{"draft"}; // draft | pending_approval | active | done | aborted | archived
    std::string created_at;
    std::string approved_by;
    std::string approved_at;
    std::vector<Acceptance> acceptance;
    std::vector<LineRef> lines;
};

struct Checkpoint {
    std::string id;
    std::string ts;
    std::string status{"valid"}; // valid | stale | orphaned | invalidated
    nlohmann::json entity = nlohmann::json::object();
    nlohmann::json git = nlohmann::json::object();
};

struct Event {
    std::string ts;
    std::string actor;
    std::string op;
    std::string reason;
    std::string checkpoint;
    nlohmann::json entity = nlohmann::json::object();
    nlohmann::json before = nullptr;
    nlohmann::json after = nullptr;
    nlohmann::json context = nlohmann::json::object();
};

nlohmann::json to_json(const Acceptance& value);
nlohmann::json to_json(const Step& value);
nlohmann::json to_json(const Task& value);
nlohmann::json to_json(const Line& value);
nlohmann::json to_json(const Plan& value);
nlohmann::json to_json(const Checkpoint& value);
nlohmann::json to_json(const Event& value);

Acceptance acceptance_from_json(const nlohmann::json& value);
Step step_from_json(const nlohmann::json& value);
Task task_from_json(const nlohmann::json& value);
Line line_from_json(const nlohmann::json& value);
Plan plan_from_json(const nlohmann::json& value);
Checkpoint checkpoint_from_json(const nlohmann::json& value);
Event event_from_json(const nlohmann::json& value);

// Current UTC timestamp in "2026-10-30T10:00:00Z" form.
std::string now_utc();

} // namespace core::model
