#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "core/id.hpp"
#include "cli/nextgen.hpp"
#include "core/root.hpp"
#include "core/template.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

void register_new(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        std::string slug;
    };
    auto args = std::make_shared<Args>();

    auto* cmd_new = app.add_subcommand("new", "Create a new change with design/ and work/ directories");
    cmd_new->set_help_all_flag("--help-all", "Expand all help");
    add_global_opts(cmd_new, global_opts);
    cmd_new->add_option("--slug", args->slug, "Slug for the change ID (lowercase, numbers, hyphens)")->required();
    cmd_new->footer("Example:\n  sele new --slug my-feature");
    cmd_new->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        if (!ng::legacy_guard(global_opts, output, exit_code, "new")) return;
        try {
            fs::path root = global_opts.root.empty() ? fs::current_path() : global_opts.root;

            if (!core::is_initialized(root)) {
                output.error("Project is not initialized (missing .design/ or .agent/).",
                             "sele init");
                exit_code = exit_codes::ENV_ERROR;
                return;
            }

            fs::path design_root = core::find_design_root(root);
            fs::path agent_root = core::find_agent_root(root);

            if (!core::validate_slug(args->slug)) {
                output.error("Invalid slug: must contain only lowercase letters, numbers, and hyphens",
                             "sele new --slug valid-slug-name");
                exit_code = exit_codes::USAGE_ERROR;
                return;
            }

            std::string id = core::generate_id(args->slug);
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

            std::ostringstream text;
            text << "Created " << id << '\n'
                 << "  design: " << design_md.string() << '\n'
                 << "  work:   " << tasks_md.string() << '\n'
                 << "  state:  Active Change -> " << id;

            output.print_both(text.str(), result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
