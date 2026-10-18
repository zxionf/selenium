#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

#include "core/root.hpp"
#include "core/id.hpp"
#include "core/template.hpp"
#include "core/git.hpp"
#include "util/output.hpp"
#include "util/exit_codes.hpp"

namespace fs = std::filesystem;

struct GlobalOptions {
    fs::path root;
    util::Output::Format output_format = util::Output::Format::TEXT;
};

int main(int argc, char** argv) {
    CLI::App app{"design - Manage .design/ and .agent/ directories"};
    app.require_subcommand(1);

    GlobalOptions global_opts;
    int exit_code = exit_codes::SUCCESS;

    app.add_option("--root", global_opts.root, "Project root directory (default: current dir, search upward for .design/)");
    app.add_option("--output", global_opts.output_format, "Output format: text or json")
        ->transform(CLI::CheckedTransformer(std::map<std::string, util::Output::Format>{
            {"text", util::Output::Format::TEXT},
            {"json", util::Output::Format::JSON}
        }, CLI::ignore_case));

    auto add_global_opts = [&](CLI::App* sub) {
        sub->add_option("--root", global_opts.root, "Project root directory")
            ->capture_default_str();
        sub->add_option("--output", global_opts.output_format, "Output format: text or json")
            ->transform(CLI::CheckedTransformer(std::map<std::string, util::Output::Format>{
                {"text", util::Output::Format::TEXT},
                {"json", util::Output::Format::JSON}
            }, CLI::ignore_case))
            ->capture_default_str();
    };

    // design new
    std::string new_slug;
    auto* cmd_new = app.add_subcommand("new", "Create a new change with design/ and work/ directories");
    cmd_new->set_help_all_flag("--help-all", "Expand all help");
    add_global_opts(cmd_new);
    cmd_new->add_option("--slug", new_slug, "Slug for the change ID (lowercase, numbers, hyphens)")->required();
    cmd_new->footer("Example:\n  design new --slug my-feature");
    cmd_new->callback([&]() {
        util::Output output(global_opts.output_format);
        try {
            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);
            fs::path agent_root = core::find_agent_root(root);

            if (!core::validate_slug(new_slug)) {
                output.error("Invalid slug: must contain only lowercase letters, numbers, and hyphens",
                             "design new --slug valid-slug-name");
                exit_code = exit_codes::USAGE_ERROR;
                return;
            }

            std::string id = core::generate_id(new_slug);
            fs::path changes_dir = design_root / ".design" / "changes" / id;
            fs::path work_dir = agent_root / ".agent" / "work" / id;

            if (fs::exists(changes_dir) || fs::exists(work_dir)) {
                output.error("ID already exists: " + id, "");
                exit_code = exit_codes::STATE_CONFLICT;
                return;
            }

            fs::create_directories(changes_dir);
            fs::create_directories(work_dir);

            fs::path design_md = changes_dir / "design.md";
            std::ofstream design_file(design_md);
            design_file << core::render_design_template(id);
            design_file.close();

            fs::path tasks_md = work_dir / "tasks.md";
            std::ofstream tasks_file(tasks_md);
            tasks_file << core::render_tasks_template(id);
            tasks_file.close();

            fs::path state_md = agent_root / ".agent" / "STATE.md";
            std::string state_content;
            if (fs::exists(state_md)) {
                std::ifstream in(state_md);
                state_content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            } else {
                state_content = "# Agent State\n\nActive Change: 无\n";
            }

            std::string new_state;
            std::string active_marker = "Active Change:";
            size_t pos = state_content.find(active_marker);
            if (pos != std::string::npos) {
                size_t line_end = state_content.find('\n', pos);
                if (line_end == std::string::npos) line_end = state_content.size();
                new_state = state_content.substr(0, pos) + "Active Change: " + id + state_content.substr(line_end);
            } else {
                new_state = state_content + "\nActive Change: " + id + "\n";
            }

            std::ofstream state_file(state_md);
            state_file << new_state;
            state_file.close();

            nlohmann::json result;
            result["id"] = id;
            result["design_path"] = design_md.string();
            result["work_path"] = tasks_md.string();
            result["state_updated"] = true;

            output.print_json(result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });

    // design append-decision
    std::string append_context, append_decision, append_consequences, append_change_id;
    auto* cmd_append = app.add_subcommand("append-decision", "Append a decision record to DECISIONS.log");
    add_global_opts(cmd_append);
    cmd_append->add_option("--context", append_context, "Context for the decision")->required();
    cmd_append->add_option("--decision", append_decision, "The decision made")->required();
    cmd_append->add_option("--consequences", append_consequences, "Consequences of the decision")->required();
    cmd_append->add_option("--change-id", append_change_id, "Associated change ID (optional)");
    cmd_append->footer("Example:\n  design append-decision --context \"Use C++20\" --decision \"Adopt modules\" --consequences \"Cleaner imports\" --change-id 20261018-my-feature");
    cmd_append->callback([&]() {
        util::Output output(global_opts.output_format);
        try {
            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);

            fs::path decisions_log = design_root / ".design" / "DECISIONS.log";

            int index = 1;
            if (fs::exists(decisions_log)) {
                std::ifstream in(decisions_log);
                std::string line;
                while (std::getline(in, line)) {
                    if (line.rfind("## [", 0) == 0) {
                        index++;
                    }
                }
            }

            std::ofstream out(decisions_log, std::ios::app);
            if (!fs::exists(decisions_log) || fs::file_size(decisions_log) == 0) {
                out << core::render_decisions_header();
            }
            out << core::render_decision_entry(index,
                append_change_id.empty() ? "none" : append_change_id,
                append_context, append_decision, append_consequences);
            out.close();

            nlohmann::json result;
            result["appended"] = true;
            result["index"] = index;
            result["path"] = decisions_log.string();

            output.print_json(result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });

    // design status
    auto* cmd_status = app.add_subcommand("status", "Show current active change status");
    add_global_opts(cmd_status);
    cmd_status->footer("Example:\n  design status --output json");
    cmd_status->callback([&]() {
        util::Output output(global_opts.output_format);
        try {
            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);
            fs::path agent_root = core::find_agent_root(root);

            fs::path state_md = agent_root / ".agent" / "STATE.md";
            std::string active_id;
            if (fs::exists(state_md)) {
                std::ifstream in(state_md);
                std::string line;
                while (std::getline(in, line)) {
                    if (line.rfind("Active Change:", 0) == 0) {
                        active_id = line.substr(14);
                        // trim
                        while (!active_id.empty() && isspace(active_id.front())) active_id.erase(active_id.begin());
                        while (!active_id.empty() && isspace(active_id.back())) active_id.pop_back();
                        if (active_id == "无") active_id.clear();
                        break;
                    }
                }
            }

            nlohmann::json result;
            result["active_id"] = active_id;
            result["tasks"] = nlohmann::json{{"total", 0}, {"done", 0}};
            result["decisions"] = 0;
            result["consistent"] = true;

            if (!active_id.empty()) {
                fs::path tasks_md = agent_root / ".agent" / "work" / active_id / "tasks.md";
                if (fs::exists(tasks_md)) {
                    std::ifstream in(tasks_md);
                    std::string line;
                    int total = 0, done = 0;
                    while (std::getline(in, line)) {
                        if (line.rfind("- [ ]", 0) == 0) total++;
                        else if (line.rfind("- [x]", 0) == 0 || line.rfind("- [X]", 0) == 0) {
                            total++;
                            done++;
                        }
                    }
                    result["tasks"]["total"] = total;
                    result["tasks"]["done"] = done;
                }

                fs::path decisions_log = design_root / ".design" / "DECISIONS.log";
                if (fs::exists(decisions_log)) {
                    std::ifstream in(decisions_log);
                    std::string line;
                    int count = 0;
                    while (std::getline(in, line)) {
                        if (line.find("Change: " + active_id) != std::string::npos) {
                            count++;
                        }
                    }
                    result["decisions"] = count;
                }

                fs::path changes_dir = design_root / ".design" / "changes" / active_id;
                fs::path work_dir = agent_root / ".agent" / "work" / active_id;
                bool changes_exist = fs::exists(changes_dir);
                bool work_exist = fs::exists(work_dir);
                result["consistent"] = (changes_exist == work_exist);
            }

            output.print_json(result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });

    // design lint
    bool lint_fix = false;
    auto* cmd_lint = app.add_subcommand("lint", "Run mechanical rule checks on design/ and agent/");
    add_global_opts(cmd_lint);
    cmd_lint->add_flag("--fix", lint_fix, "Auto-fix missing template files (dir_integrity only)");
    cmd_lint->footer("Example:\n  design lint\n  design lint --fix");
    cmd_lint->callback([&]() {
        util::Output output(global_opts.output_format);
        try {
            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);
            fs::path agent_root = core::find_agent_root(root);

            nlohmann::json checks = nlohmann::json::array();
            bool all_passed = true;

            // 1. ID consistency
            bool id_consistency_passed = true;
            fs::path changes_base = design_root / ".design" / "changes";
            fs::path work_base = agent_root / ".agent" / "work";

            if (fs::exists(changes_base)) {
                for (auto& entry : fs::directory_iterator(changes_base)) {
                    if (entry.is_directory()) {
                        fs::path work_dir = work_base / entry.path().filename();
                        if (!fs::exists(work_dir)) {
                            id_consistency_passed = false;
                            break;
                        }
                    }
                }
            }
            if (id_consistency_passed && fs::exists(work_base)) {
                for (auto& entry : fs::directory_iterator(work_base)) {
                    if (entry.is_directory()) {
                        fs::path changes_dir = changes_base / entry.path().filename();
                        if (!fs::exists(changes_dir)) {
                            id_consistency_passed = false;
                            break;
                        }
                    }
                }
            }
            checks.push_back({{"name", "id_consistency"}, {"passed", id_consistency_passed}});
            if (!id_consistency_passed) all_passed = false;

            // 2. DECISIONS.log append-only
            bool decisions_append_passed = true;
            nlohmann::json decisions_detail;
            fs::path decisions_log = design_root / ".design" / "DECISIONS.log";
            if (core::is_git_repo(design_root) && core::has_head(design_root)) {
                try {
                    std::string diff = core::execute_git_silent(design_root, {"diff", "--no-color", "--", ".design/DECISIONS.log"});
                    std::istringstream iss(diff);
                    std::string line;
                    int deleted_lines = 0;
                    while (std::getline(iss, line)) {
                        if (line.rfind("-", 0) == 0 && line.rfind("---", 0) != 0) {
                            deleted_lines++;
                        }
                    }
                    if (deleted_lines > 0) {
                        decisions_append_passed = false;
                        decisions_detail["deleted_lines"] = deleted_lines;
                    }
                } catch (...) {
                    decisions_detail["skipped"] = true;
                }
            } else {
                decisions_detail["skipped"] = true;
            }
            checks.push_back({{"name", "decisions_append_only"}, {"passed", decisions_append_passed}, {"detail", decisions_detail}});
            if (!decisions_append_passed) all_passed = false;

            // 3. SPEC.md process words
            bool spec_process_passed = true;
            fs::path spec_md = design_root / ".design" / "SPEC.md";
            if (fs::exists(spec_md)) {
                std::ifstream in(spec_md);
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                std::transform(content.begin(), content.end(), content.begin(), ::tolower);
                static const std::vector<std::string> forbidden = {"todo", "wip", "待办", "下一步", "进行中"};
                for (const auto& word : forbidden) {
                    if (content.find(word) != std::string::npos) {
                        spec_process_passed = false;
                        break;
                    }
                }
            }
            checks.push_back({{"name", "spec_process_words"}, {"passed", spec_process_passed}});
            if (!spec_process_passed) all_passed = false;

            // 4. Directory integrity
            bool dir_integrity_passed = true;
            if (fs::exists(changes_base)) {
                for (auto& entry : fs::directory_iterator(changes_base)) {
                    if (entry.is_directory()) {
                        fs::path design_md = entry.path() / "design.md";
                        if (!fs::exists(design_md)) {
                            if (lint_fix) {
                                std::ofstream out(design_md);
                                out << core::render_design_template(entry.path().filename().string());
                            } else {
                                dir_integrity_passed = false;
                            }
                        }
                    }
                }
            }
            if (dir_integrity_passed && fs::exists(work_base)) {
                for (auto& entry : fs::directory_iterator(work_base)) {
                    if (entry.is_directory()) {
                        fs::path tasks_md = entry.path() / "tasks.md";
                        if (!fs::exists(tasks_md)) {
                            if (lint_fix) {
                                std::ofstream out(tasks_md);
                                out << core::render_tasks_template(entry.path().filename().string());
                            } else {
                                dir_integrity_passed = false;
                            }
                        }
                    }
                }
            }
            checks.push_back({{"name", "dir_integrity"}, {"passed", dir_integrity_passed}});
            if (!dir_integrity_passed) all_passed = false;

            nlohmann::json result;
            result["passed"] = all_passed;
            result["checks"] = checks;

            output.print_json(result);
            exit_code = all_passed ? exit_codes::SUCCESS : exit_codes::LINT_FAILED;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });

    // design close
    std::string close_id;
    bool close_dry_run = false;
    auto* cmd_close = app.add_subcommand("close", "Close a change (MVP: --dry-run only)");
    add_global_opts(cmd_close);
    cmd_close->add_option("id", close_id, "Change ID to close")->required();
    cmd_close->add_flag("--dry-run", close_dry_run, "Preview only (required for MVP)");
    cmd_close->footer("Example:\n  design close 20261018-my-feature --dry-run");
    cmd_close->callback([&]() {
        util::Output output(global_opts.output_format);
        try {
            if (!close_dry_run) {
                output.error("MVP does not implement actual close", "design close <id> --dry-run");
                exit_code = exit_codes::USAGE_ERROR;
                return;
            }

            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);
            fs::path agent_root = core::find_agent_root(root);

            fs::path changes_dir = design_root / ".design" / "changes" / close_id;
            fs::path work_dir = agent_root / ".agent" / "work" / close_id;

            if (!fs::exists(changes_dir) || !fs::exists(work_dir)) {
                output.error("Both .design/changes/<id> and .agent/work/<id> must exist", "");
                exit_code = exit_codes::STATE_CONFLICT;
                return;
            }

            fs::path design_md = changes_dir / "design.md";
            std::string design_content;
            if (fs::exists(design_md)) {
                std::ifstream in(design_md);
                design_content.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            }

            fs::path spec_md = design_root / ".design" / "SPEC.md";
            std::vector<std::string> spec_sections;
            if (fs::exists(spec_md)) {
                std::ifstream in(spec_md);
                std::string line;
                while (std::getline(in, line)) {
                    if (line.rfind("## ", 0) == 0) {
                        spec_sections.push_back(line.substr(3));
                    }
                }
            }

            std::vector<std::string> design_sections = {"Context", "变更内容", "影响面", "兼容性与迁移"};
            nlohmann::json suggested_mapping = nlohmann::json::object();
            for (const auto& ds : design_sections) {
                bool matched = false;
                for (const auto& ss : spec_sections) {
                    if (ss.find(ds) != std::string::npos || ds.find(ss) != std::string::npos) {
                        suggested_mapping[ds] = ss;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    suggested_mapping[ds] = spec_sections.empty() ? "(no sections)" : spec_sections[0];
                }
            }

            nlohmann::json result;
            result["dry_run"] = true;
            result["id"] = close_id;
            result["design_sections"] = design_sections;
            result["spec_sections"] = spec_sections;
            result["suggested_mapping"] = suggested_mapping;

            output.print_json(result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });

    try {
        app.parse(argc, argv);
    } catch (const CLI::CallForHelp& e) {
        std::cout << app.help() << '\n';
        std::exit(exit_codes::SUCCESS);
    } catch (const CLI::CallForAllHelp& e) {
        std::cout << app.help("", CLI::AppFormatMode::All) << '\n';
        std::exit(exit_codes::SUCCESS);
    } catch (const CLI::ParseError& e) {
        util::Output output(global_opts.output_format);
        if (global_opts.output_format == util::Output::Format::JSON) {
            nlohmann::json err;
            err["error"] = e.what();
            err["suggestion"] = "Run with --help for usage";
            std::cerr << err.dump() << '\n';
        } else {
            std::cerr << "Error: " << e.what() << '\n';
            std::cerr << "Suggestion: Run with --help for usage\n";
        }
        std::exit(exit_codes::USAGE_ERROR);
    } catch (const std::exception& e) {
        util::Output output(global_opts.output_format);
        output.error(e.what(), "Run with --help for usage");
        std::exit(exit_codes::ENV_ERROR);
    }

    return exit_code;
}