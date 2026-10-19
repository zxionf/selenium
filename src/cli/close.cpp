#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "core/root.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

void register_close(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        std::string id;
        bool dry_run = false;
    };
    auto args = std::make_shared<Args>();

    auto* cmd_close = app.add_subcommand("close", "Close a change (MVP: --dry-run only)");
    add_global_opts(cmd_close, global_opts);
    cmd_close->add_option("id", args->id, "Change ID to close")->required();
    cmd_close->add_flag("--dry-run", args->dry_run, "Preview only (required for MVP)");
    cmd_close->footer("Example:\n  design close 20261018-my-feature --dry-run");
    cmd_close->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        try {
            if (!args->dry_run) {
                output.error("MVP does not implement actual close", "design close <id> --dry-run");
                exit_code = exit_codes::USAGE_ERROR;
                return;
            }

            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;
            fs::path design_root = core::find_design_root(root);
            fs::path agent_root = core::find_agent_root(root);

            fs::path changes_dir = design_root / ".design" / "changes" / args->id;
            fs::path work_dir = agent_root / ".agent" / "work" / args->id;

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
            result["id"] = args->id;
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
}

} // namespace cli
