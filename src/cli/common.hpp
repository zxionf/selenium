#pragma once

#include "core/store.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <memory>
#include <string>
#include <vector>

struct GlobalOptions {
    std::string root{"."};
    std::string output{"text"};
};

void add_global_opts(CLI::App* app, GlobalOptions& options);

struct Context {
    core::store::Paths paths;
    util::Output out;
};

std::shared_ptr<Context> make_context(const GlobalOptions& options);

std::vector<std::string> split_csv(const std::string& value);

// Print an error in the active output format and set the process exit code.
void fail(Context& ctx, int& exit_code, int code, const std::string& message,
          const std::string& suggestion = "");

void append_event(const core::store::Paths& paths, const std::string& actor,
                  const std::string& op, const nlohmann::json& entity,
                  const nlohmann::json& before, const nlohmann::json& after,
                  const std::string& reason);

void register_init_cmd(CLI::App& app, GlobalOptions& global, int& exit_code);
void register_version_cmd(CLI::App& app, GlobalOptions& global, int& exit_code);
void register_plan_cmds(CLI::App& app, GlobalOptions& global, int& exit_code);
void register_line_cmds(CLI::App& app, GlobalOptions& global, int& exit_code);
void register_task_cmds(CLI::App& app, GlobalOptions& global, int& exit_code);
void register_step_cmds(CLI::App& app, GlobalOptions& global, int& exit_code);
