#include "core/sele.hpp"

#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

#include "core/fs.hpp"
#include "core/gitrt.hpp"

namespace core::sele {

namespace {

bool dir_exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

std::string trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
    return value.substr(begin, end - begin);
}

} // namespace

std::optional<std::filesystem::path> find_root(const std::filesystem::path& start) {
    std::filesystem::path current = std::filesystem::absolute(start);
    while (true) {
        if (dir_exists(current / ".sele")) return current;
        const auto parent = current.parent_path();
        if (parent == current) break;
        current = parent;
    }
    return std::nullopt;
}

Layout layout(const std::filesystem::path& root) {
    Layout out;
    out.root = root;
    out.sele = root / ".sele";
    out.lines = out.sele / "lines";
    out.runtime = out.sele / "runtime";
    out.state = out.runtime / "state";
    out.sessions = out.runtime / "sessions";
    out.log = out.sele / "log.jsonl";
    out.events = out.runtime / "events.jsonl";
    return out;
}

void ensure_runtime(const Layout& layout) {
    fsys::ensure_dir(layout.lines);
    fsys::ensure_dir(layout.state);
    fsys::ensure_dir(layout.sessions);
}

Config load_config(const Layout& layout) {
    Config config;
    const auto path = layout.sele / "config.json";
    if (!fsys::exists(path)) return config;

    const json root = read_json(path);
    const int schema = root.value("schema_version", 1);
    if (schema != 1) {
        throw std::runtime_error("Unsupported .sele schema_version: " + std::to_string(schema));
    }
    config.schema_version = schema;
    if (root.contains("actor") && root["actor"].is_string()) {
        config.actor = root["actor"].get<std::string>();
    }
    if (root.contains("modules") && root["modules"].is_object()) {
        const auto& modules = root["modules"];
        if (modules.contains("allow") && modules["allow"].is_array()) {
            for (const auto& item : modules["allow"]) {
                if (item.is_string()) config.modules_allow.push_back(item.get<std::string>());
            }
        }
    }
    if (root.contains("checkpoint") && root["checkpoint"].is_object()) {
        const auto& checkpoint = root["checkpoint"];
        if (checkpoint.contains("strategy") && checkpoint["strategy"].is_string()) {
            config.checkpoint_strategy = checkpoint["strategy"].get<std::string>();
        }
        if (checkpoint.contains("allow_empty") && checkpoint["allow_empty"].is_boolean()) {
            config.allow_empty = checkpoint["allow_empty"].get<bool>();
        }
    }
    if (config.checkpoint_strategy != "git-commit") {
        throw std::runtime_error("Unsupported checkpoint strategy: " + config.checkpoint_strategy);
    }
    return config;
}

std::string resolve_actor(const Config& config) {
    if (const char* env = std::getenv("SELE_ACTOR"); env != nullptr && env[0] != '\0') return env;
    if (config.actor != "git") return config.actor;

    const auto result = gitrt::run(std::filesystem::current_path(), {"config", "user.name"});
    if (result.code == 0) {
        const std::string name = trim(result.out);
        if (!name.empty()) return name;
    }
    return "unknown";
}

std::string utc_now() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string utc_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y%m%d");
    return out.str();
}

bool valid_slug(const std::string& slug) {
    static const std::regex pattern("^[a-z0-9][a-z0-9-]*$");
    return std::regex_match(slug, pattern);
}

bool valid_line_id(const std::string& id) {
    static const std::regex pattern("^\\d{8}-[a-z0-9][a-z0-9-]*$");
    return std::regex_match(id, pattern);
}

std::filesystem::path line_dir(const Layout& layout, const std::string& id) {
    return layout.lines / id;
}

std::filesystem::path line_file(const Layout& layout, const std::string& id) {
    return layout.lines / id / "line.json";
}

std::filesystem::path tasks_dir(const Layout& layout, const std::string& id) {
    return layout.lines / id / "tasks";
}

std::filesystem::path state_file(const Layout& layout, const std::string& id) {
    return layout.state / (id + ".json");
}

json read_json(const std::filesystem::path& path) {
    const std::string content = fsys::read_file(path);
    if (content.empty()) throw std::runtime_error("Missing or empty JSON file: " + path.string());
    json value;
    try {
        value = json::parse(content);
    } catch (const json::parse_error& error) {
        throw std::runtime_error("Invalid JSON in " + path.string() + ": " + error.what());
    }
    if (!value.is_object()) {
        throw std::runtime_error("Expected a JSON object in " + path.string());
    }
    const int schema = value.value("schema_version", 0);
    if (schema != 1) {
        throw std::runtime_error("Unsupported schema_version in " + path.string() +
                                 " (expected 1)");
    }
    return value;
}

void write_json(const std::filesystem::path& path, const json& value) {
    fsys::write_atomic(path, value.dump(2) + "\n");
}

} // namespace core::sele
