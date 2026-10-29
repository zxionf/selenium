#include "core/session.hpp"

#include <cstdlib>

#include "core/fs.hpp"

namespace core::session {

std::string resolve_id(const std::string& override_id) {
    if (!override_id.empty()) return override_id;
    if (const char* env = std::getenv("SELE_SESSION"); env != nullptr && env[0] != '\0') return env;
    return "default";
}

std::filesystem::path path_of(const sele::Layout& layout, const std::string& session_id) {
    return layout.sessions / (session_id + ".json");
}

Pointer read(const sele::Layout& layout, const std::string& session_id) {
    Pointer pointer;
    const auto path = path_of(layout, session_id);
    if (!fsys::exists(path)) return pointer;

    const auto value = sele::read_json(path);
    if (value.contains("line") && value["line"].is_string()) {
        pointer.line = value["line"].get<std::string>();
    }
    if (value.contains("task") && value["task"].is_string()) {
        pointer.task = value["task"].get<std::string>();
    }
    return pointer;
}

void write(const sele::Layout& layout, const std::string& session_id, const Pointer& pointer) {
    fsys::ensure_dir(layout.sessions);
    sele::json value = sele::json::object();
    value["schema_version"] = 1;
    value["session"] = session_id;
    value["line"] = pointer.line;
    value["task"] = pointer.task;
    value["updated_at"] = sele::utc_now();
    sele::write_json(path_of(layout, session_id), value);
}

} // namespace core::session
