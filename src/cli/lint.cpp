#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/git.hpp"
#include "cli/nextgen.hpp"
#include "core/root.hpp"
#include "core/template.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

void register_lint(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        bool fix = false;
    };
    auto args = std::make_shared<Args>();

    auto* cmd_lint = app.add_subcommand("lint", "Run mechanical rule checks on design/ and agent/");
    add_global_opts(cmd_lint, global_opts);
    cmd_lint->add_flag("--fix", args->fix, "Auto-fix missing template files (dir_integrity only)");
    cmd_lint->footer("Example:\n  sele lint\n  sele lint --fix");
    cmd_lint->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        if (!ng::legacy_guard(global_opts, output, exit_code, "lint")) return;
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
                            if (args->fix) {
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
                            if (args->fix) {
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

            std::ostringstream text;
            text << (all_passed ? "Lint passed" : "Lint failed");
            for (const auto& check : checks) {
                std::string tag = "ok";
                const auto& name = check["name"].get<std::string>();
                if (!check["passed"].get<bool>()) {
                    tag = "fail";
                } else if (check.contains("detail") && check["detail"].is_object() &&
                           check["detail"].value("skipped", false)) {
                    tag = "skip";
                }
                text << "\n  [" << tag << "] " << name;
                if (tag == "fail" && check.contains("detail") && check["detail"].is_object() &&
                    check["detail"].contains("deleted_lines")) {
                    text << " (deleted_lines: " << check["detail"]["deleted_lines"].get<int>() << ")";
                }
            }

            output.print_both(text.str(), result);
            exit_code = all_passed ? exit_codes::SUCCESS : exit_codes::LINT_FAILED;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
