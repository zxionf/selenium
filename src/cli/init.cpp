#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

#include "core/fs.hpp"
#include "core/sele.hpp"
#include "core/template.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

namespace {

void write_file(const fs::path& path, const std::string& content) {
    std::ofstream out(path);
    out << content;
}

void init_legacy(const fs::path& target, util::Output& output, int& exit_code) {
    fs::path design_dir = target / ".design";
    fs::path agent_dir = target / ".agent";

    if (fs::exists(design_dir) || fs::exists(agent_dir)) {
        output.error("Already initialized: .design/ or .agent/ already exists in " + target.string(),
                     "");
        exit_code = exit_codes::STATE_CONFLICT;
        return;
    }

    fs::create_directories(design_dir / "changes");
    fs::create_directories(agent_dir / "work");

    write_file(design_dir / "INDEX.md", core::render_design_index_template());
    write_file(design_dir / "SPEC.md", core::render_spec_template());
    write_file(design_dir / "DECISIONS.log", core::render_decisions_header());
    write_file(agent_dir / "INDEX.md", core::render_agent_index_template());
    write_file(agent_dir / "CONTRACT.md", core::render_contract_template());
    write_file(agent_dir / "STATE.md", core::render_state_template());

    nlohmann::json result;
    result["initialized"] = true;
    result["mode"] = "legacy";
    result["root"] = target.string();
    result["design_path"] = design_dir.string();
    result["agent_path"] = agent_dir.string();

    std::ostringstream text;
    text << "Initialized .design/ and .agent/ in " << target.string() << '\n'
         << "  design: " << design_dir.string() << '\n'
         << "  agent:  " << agent_dir.string();

    output.print_both(text.str(), result);
    exit_code = exit_codes::SUCCESS;
}

void init_next(const fs::path& target, util::Output& output, int& exit_code) {
    const auto layout = core::sele::layout(target);
    if (core::fsys::exists(layout.sele)) {
        output.error("Already initialized: .sele/ already exists in " + target.string(),
                     "Use --legacy to create .design/.agent instead");
        exit_code = exit_codes::STATE_CONFLICT;
        return;
    }

    // Coexistence is allowed: keep legacy .design/.agent untouched while adding .sele/.
    const bool legacy_present = core::fsys::exists(target / ".design") ||
                                core::fsys::exists(target / ".agent");

    core::sele::json config = core::sele::json::object();
    config["schema_version"] = 1;
    config["actor"] = "git";
    config["modules"] = core::sele::json::object();
    config["modules"]["allow"] = core::sele::json::array();
    config["checkpoint"] = core::sele::json::object();
    config["checkpoint"]["strategy"] = "git-commit";
    config["checkpoint"]["allow_empty"] = false;

    core::fsys::ensure_dir(layout.lines);
    core::fsys::ensure_dir(layout.state);
    core::fsys::ensure_dir(layout.sessions);
    core::fsys::write_atomic(layout.sele / "config.json", config.dump(2) + "\n");
    core::fsys::write_atomic(layout.log, std::string());
    core::fsys::write_atomic(layout.sele / ".gitignore", "runtime/\n");
    core::fsys::write_atomic(layout.sele / ".gitattributes", "log.jsonl merge=union\n");

    // AGENTS.md is the agent entry point; never overwrite a user-provided one.
    const auto agents = target / "AGENTS.md";
    bool agents_written = false;
    if (!core::fsys::exists(agents)) {
        core::fsys::write_atomic(agents, core::render_agents_template());
        agents_written = true;
    }

    nlohmann::json result;
    result["initialized"] = true;
    result["mode"] = "next";
    result["root"] = target.string();
    result["sele_path"] = layout.sele.string();
    result["agents_path"] = agents.string();
    result["agents_written"] = agents_written;
    result["legacy_present"] = legacy_present;

    std::ostringstream text;
    text << "Initialized .sele/ in " << target.string() << '\n'
         << "  sele: " << layout.sele.string() << '\n'
         << "  agents: " << agents.string()
         << (agents_written ? "" : " (existing, untouched)") << '\n';
    if (legacy_present) {
        text << "  legacy: .design/.agent present (kept untouched)\n";
    }
    text << "  next: sele line new <slug> --goal \"...\" --acceptance \"desc::check\" --modules a,b";
    output.print_both(text.str(), result);
    exit_code = exit_codes::SUCCESS;
}

} // namespace

void register_init(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        std::string directory;
        bool legacy = false;
    };
    auto args = std::make_shared<Args>();

    auto* cmd_init = app.add_subcommand("init", "Initialize .sele/ (or .design/ + .agent/ with --legacy)");
    cmd_init->add_option("--root", global_opts.root, "Target directory to initialize (default: current directory)");
    add_output_opt(cmd_init, global_opts);
    cmd_init->add_option("directory", args->directory, "Target directory (overrides --root)");
    cmd_init->add_flag("--legacy", args->legacy, "Create .design/ and .agent/ instead of .sele/");
    cmd_init->footer("Example:\n  sele init\n  sele init --legacy\n  sele init ./my-project");
    cmd_init->callback([&global_opts, &exit_code, args]() {
        util::Output output(global_opts.output_format);
        try {
            fs::path target;
            if (!args->directory.empty()) {
                target = args->directory;
            } else if (!global_opts.root.empty()) {
                target = global_opts.root;
            } else {
                target = fs::current_path();
            }
            target = fs::absolute(target);

            if (args->legacy) {
                init_legacy(target, output, exit_code);
            } else {
                init_next(target, output, exit_code);
            }
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
