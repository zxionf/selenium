#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "cli/commands.hpp"
#include "core/model.hpp"
#include "core/sele.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace cli::ng {

using json = core::sele::json;

struct CliError {
    int code = exit_codes::ENV_ERROR;
    std::string message;
    std::string suggestion;
    std::vector<std::string> next;
};

[[noreturn]] void fail(int code, std::string message, std::string suggestion = {},
                       std::vector<std::string> next = {});

template <typename Fn>
int handle(util::Output& out, Fn&& body) {
    try {
        body();
        return exit_codes::SUCCESS;
    } catch (const CliError& error) {
        out.error(error.message, error.suggestion, error.next);
        return error.code;
    } catch (const std::exception& error) {
        out.error(error.what(), "");
        return exit_codes::ENV_ERROR;
    }
}

struct Ctx {
    core::sele::Layout lay;
    core::sele::Config conf;
    std::string actor;
    std::string session;
    std::string line_id;
    core::Line line;
    core::RuntimeState rt;
    std::vector<core::TaskSpec> tasks;

    core::TaskSpec* task(const std::string& id);
    bool has_task(const std::string& id) const;
    core::TaskState& task_state(const std::string& id);
    core::AcceptanceState& acc_state(const std::string& id);
};

core::sele::Layout require_layout(const GlobalOptions& opts);

// Load layout + config + session only (no line). Used by line new / list.
Ctx load_root(const GlobalOptions& opts);

// Load the full context for a line: explicit id, or the session pointer.
Ctx load(const GlobalOptions& opts, const std::string& explicit_line);

void save_line(Ctx& ctx);
void save_runtime(Ctx& ctx);
void save_task(Ctx& ctx, const core::TaskSpec& task);

void append_log(Ctx& ctx, const std::string& op, json extra = json::object());
void append_event(Ctx& ctx, const std::string& op, json extra = json::object());

std::string last_task_id(const Ctx& ctx);
std::string next_task_id(const Ctx& ctx);
std::string status_of(const Ctx& ctx, const std::string& task_id);
int count_tasks(const Ctx& ctx, const std::string& status);
int count_acceptance(const Ctx& ctx, const std::string& status);

std::vector<std::string> split_csv(const std::string& text);
std::string join(const std::vector<std::string>& values, const std::string& separator);

void require_active(const Ctx& ctx, const std::string& action);
void check_graph(const std::vector<core::TaskSpec>& tasks, std::vector<std::string>& errors,
                 std::vector<std::string>& warnings);

// Old commands keep working in .design projects; when a .sele project exists
// without .design they must refuse with a pointer to the new commands.
bool legacy_guard(const GlobalOptions& opts, util::Output& out, int& exit_code, const char* command);

} // namespace cli::ng
