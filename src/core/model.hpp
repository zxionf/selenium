#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace core {

using ojson = nlohmann::ordered_json;

struct Acceptance {
    std::string id;
    std::string desc;
    std::string check;
};

struct Line {
    int schema_version = 1;
    int version = 1;
    std::string id;
    std::string type = "feature";
    std::string goal;
    std::vector<Acceptance> acceptance;
    std::vector<std::string> modules;
    std::string created_at;
    std::string created_by;
    std::string status = "draft";

    bool has_frozen = false;
    std::string goal_sha256;
    std::string acceptance_sha256;
    std::string frozen_at;

    bool has_blocked = false;
    std::string block_reason;
    std::string blocked_at;

    bool has_closed = false;
    std::string closed_at;
    int closed_pass = 0;
    int closed_fail = 0;

    std::string base_commit;
    bool has_merged = false;
    std::string merged_at;
    std::string merge_commit;
};

struct TaskSpec {
    int schema_version = 1;
    std::string id;
    std::string title;
    std::vector<std::string> depends;
    std::vector<std::string> modules;
    std::string created_at;
    std::string created_by;
};

struct TaskState {
    std::string status = "pending";
    std::string checkpoint;
    std::string started_at;
    std::string finished_at;
    std::string reason;
};

struct AcceptanceState {
    std::string status = "pending";
    std::string evidence;
    std::string by;
    std::string at;
};

struct RuntimeState {
    int schema_version = 1;
    int version = 1;
    std::string line;
    std::map<std::string, TaskState> tasks;
    std::map<std::string, AcceptanceState> acceptance;
};

namespace model {

ojson to_json(const Line& line);
Line line_from_json(const ojson& value);

ojson to_json(const TaskSpec& task);
TaskSpec task_from_json(const ojson& value);

ojson to_json(const RuntimeState& runtime);
RuntimeState runtime_from_json(const ojson& value);

std::string goal_digest(const Line& line);
std::string acceptance_digest(const Line& line);

// Human/digest friendly summary of acceptance results for a finished line.
struct AcceptanceSummary {
    int passed = 0;
    int failed = 0;
    int pending = 0;
};
AcceptanceSummary summarize(const Line& line, const RuntimeState& runtime);

} // namespace model

// Convenience: core::goal_digest / core::acceptance_digest
using model::acceptance_digest;
using model::goal_digest;

} // namespace core
