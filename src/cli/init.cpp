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

#include <algorithm>
#include <vector>

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
    const auto agents = target / "AGENTS.md";
    const auto contract = layout.sele / "CONTRACT.md";
    const bool existed = core::fsys::exists(layout.sele);
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

    std::vector<std::string> created;
    auto ensure_file = [&](const fs::path& path, const std::string& content,
                           const std::string& label) {
        if (core::fsys::exists(path)) return;
        core::fsys::write_atomic(path, content);
        created.push_back(label);
    };
    ensure_file(layout.sele / "config.json", config.dump(2) + "\n", "config.json");
    ensure_file(layout.log, std::string(), "log.jsonl");
    ensure_file(layout.sele / ".gitignore", "runtime/\n", ".gitignore");
    ensure_file(layout.sele / ".gitattributes", "log.jsonl merge=union\n", ".gitattributes");
    ensure_file(agents, core::render_agents_template(), "AGENTS.md");
    ensure_file(contract, core::render_sele_contract_template(), ".sele/CONTRACT.md");

    const bool agents_written =
        std::find(created.begin(), created.end(), "AGENTS.md") != created.end();
    const bool contract_written =
        std::find(created.begin(), created.end(), ".sele/CONTRACT.md") != created.end();

    if (existed && created.empty()) {
        output.error("Already initialized: .sele/ already exists in " + target.string(),
                     "Use --legacy to create .design/.agent instead");
        exit_code = exit_codes::STATE_CONFLICT;
        return;
    }

    std::string created_text;
    for (std::size_t i = 0; i < created.size(); ++i) {
        if (i != 0) created_text += ", ";
        created_text += created[i];
    }

    nlohmann::json result;
    result["initialized"] = true;
    result["mode"] = existed ? "repair" : "next";
    result["repaired"] = existed;
    result["created"] = created;
    result["root"] = target.string();
    result["sele_path"] = layout.sele.string();
    result["agents_path"] = agents.string();
    result["agents_written"] = agents_written;
    result["contract_path"] = contract.string();
    result["contract_written"] = contract_written;
    result["legacy_present"] = legacy_present;

    std::ostringstream text;
    text << (existed ? "Repaired .sele/ in " : "Initialized .sele/ in ") << target.string()
         << '\n'
         << "  sele: " << layout.sele.string() << '\n'
         << "  agents: " << agents.string()
         << (agents_written ? "" : " (existing, untouched)") << '\n'
         << "  contract: " << contract.string()
         << (contract_written ? "" : " (existing, untouched)") << '\n';
    if (!created_text.empty()) text << "  created: " << created_text << '\n';
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
