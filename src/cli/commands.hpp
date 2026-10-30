#pragma once

#include <CLI/CLI.hpp>
#include <filesystem>

#include "util/output.hpp"

struct GlobalOptions {
    std::filesystem::path root;
    util::Output::Format output_format = util::Output::Format::TEXT;
    std::string session;
};

void add_output_opt(CLI::App* sub, GlobalOptions& global_opts);
void add_global_opts(CLI::App* sub, GlobalOptions& global_opts);

namespace cli {

void register_init(CLI::App& app, GlobalOptions& global_opts, int& exit_code);
void register_version(CLI::App& app, GlobalOptions& global_opts, int& exit_code);
void register_line(CLI::App& app, GlobalOptions& global_opts, int& exit_code);
void register_task(CLI::App& app, GlobalOptions& global_opts, int& exit_code);
void register_acceptance(CLI::App& app, GlobalOptions& global_opts, int& exit_code);
void register_overview(CLI::App& app, GlobalOptions& global_opts, int& exit_code);
void register_worktree(CLI::App& app, GlobalOptions& global_opts, int& exit_code);

} // namespace cli
