#include "core/model.hpp"

#include "core/hash.hpp"

namespace core {

namespace {

std::string str(const ojson& value, const char* key, const std::string& fallback = {}) {
    if (value.contains(key) && value[key].is_string()) return value[key].get<std::string>();
    return fallback;
}

std::vector<std::string> strings(const ojson& value, const char* key) {
    std::vector<std::string> out;
    if (value.contains(key) && value[key].is_array()) {
        for (const auto& item : value[key]) {
            if (item.is_string()) out.push_back(item.get<std::string>());
        }
    }
    return out;
}

ojson string_array(const std::vector<std::string>& values) {
    ojson out = ojson::array();
    for (const auto& value : values) out.push_back(value);
    return out;
}

ojson nullable(const std::string& value) {
    return value.empty() ? ojson(nullptr) : ojson(value);
}

} // namespace

namespace model {

ojson to_json(const Line& line) {
    ojson value = ojson::object();
    value["schema_version"] = line.schema_version;
    value["version"] = line.version;
    value["id"] = line.id;
    value["type"] = line.type;
    value["goal"] = line.goal;

    ojson acceptance = ojson::array();
    for (const auto& item : line.acceptance) {
        ojson entry = ojson::object();
        entry["id"] = item.id;
        entry["desc"] = item.desc;
        entry["check"] = item.check;
        acceptance.push_back(entry);
    }
    value["acceptance"] = acceptance;
    value["modules"] = string_array(line.modules);
    value["created_at"] = line.created_at;
    value["created_by"] = line.created_by;
    value["status"] = line.status;

    if (line.has_frozen) {
        ojson frozen = ojson::object();
        frozen["goal_sha256"] = line.goal_sha256;
        frozen["acceptance_sha256"] = line.acceptance_sha256;
        frozen["at"] = line.frozen_at;
        value["frozen"] = frozen;
    } else {
        value["frozen"] = nullptr;
    }

    if (line.has_blocked) {
        ojson blocked = ojson::object();
        blocked["reason"] = line.block_reason;
        blocked["at"] = line.blocked_at;
        value["blocked"] = blocked;
    } else {
        value["blocked"] = nullptr;
    }

    if (line.has_closed) {
        ojson closed = ojson::object();
        closed["at"] = line.closed_at;
        ojson summary = ojson::object();
        summary["pass"] = line.closed_pass;
        summary["fail"] = line.closed_fail;
        closed["acceptance_summary"] = summary;
        value["closed"] = closed;
    } else {
        value["closed"] = nullptr;
    }

    value["base_commit"] = line.base_commit.empty() ? ojson(nullptr) : ojson(line.base_commit);
    if (line.has_merged) {
        ojson merged = ojson::object();
        merged["at"] = line.merged_at;
        merged["commit"] = line.merge_commit;
        value["merged"] = merged;
    } else {
        value["merged"] = nullptr;
    }
    return value;
}

Line line_from_json(const ojson& value) {
    Line line;
    line.schema_version = value.value("schema_version", 1);
    line.version = value.value("version", 1);
    line.id = str(value, "id");
    line.type = str(value, "type", "feature");
    line.goal = str(value, "goal");
    line.created_at = str(value, "created_at");
    line.created_by = str(value, "created_by");
    line.status = str(value, "status", "draft");
    line.modules = strings(value, "modules");

    if (value.contains("acceptance") && value["acceptance"].is_array()) {
        for (const auto& item : value["acceptance"]) {
            Acceptance entry;
            entry.id = str(item, "id");
            entry.desc = str(item, "desc");
            entry.check = str(item, "check");
            line.acceptance.push_back(entry);
        }
    }

    if (value.contains("frozen") && value["frozen"].is_object()) {
        const auto& frozen = value["frozen"];
        line.has_frozen = true;
        line.goal_sha256 = str(frozen, "goal_sha256");
        line.acceptance_sha256 = str(frozen, "acceptance_sha256");
        line.frozen_at = str(frozen, "at");
    }
    if (value.contains("blocked") && value["blocked"].is_object()) {
        const auto& blocked = value["blocked"];
        line.has_blocked = true;
        line.block_reason = str(blocked, "reason");
        line.blocked_at = str(blocked, "at");
    }
    if (value.contains("closed") && value["closed"].is_object()) {
        const auto& closed = value["closed"];
        line.has_closed = true;
        line.closed_at = str(closed, "at");
        if (closed.contains("acceptance_summary") && closed["acceptance_summary"].is_object()) {
            const auto& summary = closed["acceptance_summary"];
            line.closed_pass = summary.value("pass", 0);
            line.closed_fail = summary.value("fail", 0);
        }
    }

    if (value.contains("base_commit") && value["base_commit"].is_string()) {
        line.base_commit = value["base_commit"].get<std::string>();
    }
    if (value.contains("merged") && value["merged"].is_object()) {
        const auto& merged = value["merged"];
        line.has_merged = true;
        line.merged_at = str(merged, "at");
        line.merge_commit = str(merged, "commit");
    }
    return line;
}

ojson to_json(const TaskSpec& task) {
    ojson value = ojson::object();
    value["schema_version"] = task.schema_version;
    value["id"] = task.id;
    value["title"] = task.title;
    value["depends"] = string_array(task.depends);
    value["modules"] = string_array(task.modules);
    value["group"] = nullable(task.group);
    value["created_at"] = task.created_at;
    value["created_by"] = task.created_by;
    return value;
}

TaskSpec task_from_json(const ojson& value) {
    TaskSpec task;
    task.schema_version = value.value("schema_version", 1);
    task.id = str(value, "id");
    task.title = str(value, "title");
    task.depends = strings(value, "depends");
    task.modules = strings(value, "modules");
    task.group = str(value, "group");
    task.created_at = str(value, "created_at");
    task.created_by = str(value, "created_by");
    return task;
}

ojson to_json(const RuntimeState& runtime) {
    ojson value = ojson::object();
    value["schema_version"] = runtime.schema_version;
    value["version"] = runtime.version;
    value["line"] = runtime.line;

    ojson tasks = ojson::object();
    for (const auto& [id, state] : runtime.tasks) {
        ojson entry = ojson::object();
        entry["status"] = state.status;
        entry["checkpoint"] = nullable(state.checkpoint);
        entry["started_at"] = nullable(state.started_at);
        entry["finished_at"] = nullable(state.finished_at);
        entry["reason"] = nullable(state.reason);
        tasks[id] = entry;
    }
    value["tasks"] = tasks;

    ojson acceptance = ojson::object();
    for (const auto& [id, state] : runtime.acceptance) {
        ojson entry = ojson::object();
        entry["status"] = state.status;
        entry["evidence"] = nullable(state.evidence);
        entry["by"] = nullable(state.by);
        entry["at"] = nullable(state.at);
        acceptance[id] = entry;
    }
    value["acceptance"] = acceptance;
    value["rebase_onto"] = nullable(runtime.rebase_onto);
    return value;
}

RuntimeState runtime_from_json(const ojson& value) {
    RuntimeState runtime;
    runtime.schema_version = value.value("schema_version", 1);
    runtime.version = value.value("version", 1);
    runtime.line = str(value, "line");
    runtime.rebase_onto = str(value, "rebase_onto");

    if (value.contains("tasks") && value["tasks"].is_object()) {
        for (auto it = value["tasks"].begin(); it != value["tasks"].end(); ++it) {
            TaskState state;
            state.status = str(it.value(), "status", "pending");
            state.checkpoint = str(it.value(), "checkpoint");
            state.started_at = str(it.value(), "started_at");
            state.finished_at = str(it.value(), "finished_at");
            state.reason = str(it.value(), "reason");
            runtime.tasks[it.key()] = state;
        }
    }
    if (value.contains("acceptance") && value["acceptance"].is_object()) {
        for (auto it = value["acceptance"].begin(); it != value["acceptance"].end(); ++it) {
            AcceptanceState state;
            state.status = str(it.value(), "status", "pending");
            state.evidence = str(it.value(), "evidence");
            state.by = str(it.value(), "by");
            state.at = str(it.value(), "at");
            runtime.acceptance[it.key()] = state;
        }
    }
    return runtime;
}

std::string goal_digest(const Line& line) {
    return hash::sha256_hex(line.goal);
}

std::string acceptance_digest(const Line& line) {
    std::string buffer;
    for (const auto& item : line.acceptance) {
        buffer += item.id;
        buffer += '\n';
        buffer += item.desc;
        buffer += '\n';
        buffer += item.check;
        buffer += '\n';
    }
    return hash::sha256_hex(buffer);
}

AcceptanceSummary summarize(const Line& line, const RuntimeState& runtime) {
    AcceptanceSummary out;
    for (const auto& item : line.acceptance) {
        const auto found = runtime.acceptance.find(item.id);
        const std::string status = found == runtime.acceptance.end() ? "pending" : found->second.status;
        if (status == "pass") ++out.passed;
        else if (status == "fail") ++out.failed;
        else ++out.pending;
    }
    return out;
}

} // namespace model

} // namespace core
