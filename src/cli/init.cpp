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

void init_next(const fs::path& target, util::Output& output, int& exit_code) {
    const auto layout = core::sele::layout(target);
    const auto agents = target / "AGENTS.md";
    const auto contract = layout.sele / "CONTRACT.md";
    const bool existed = core::fsys::exists(layout.sele);

    core::sele::json config = core::sele::json::object();
    config["schema_version"] = 1;
    config["actor"] = "git";
    config["modules"] = core::sele::json::object();
    config["modules"]["allow"] = core::sele::json::array();
    config["checkpoint"] = core::sele::json::object();
    config["checkpoint"]["strategy"] = "git-commit";
    config["checkpoint"]["allow_empty"] = false;
    config["commit"] = core::sele::json::object();
    config["commit"]["language"] = "en";
    config["commit"]["scope"] = "sele";
    config["commit"]["task_type"] = "auto";

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

    std::ostringstream text;
    text << (existed ? "Repaired .sele/ in " : "Initialized .sele/ in ") << target.string()
         << '\n'
         << "  sele: " << layout.sele.string() << '\n'
         << "  agents: " << agents.string()
         << (agents_written ? "" : " (existing, untouched)") << '\n'
         << "  contract: " << contract.string()
         << (contract_written ? "" : " (existing, untouched)") << '\n';
    if (!created_text.empty()) text << "  created: " << created_text << '\n';
    text << "  next: sele line new <slug> --goal \"...\" --acceptance \"desc::check\" --modules a,b";
    output.print_both(text.str(), result);
    exit_code = exit_codes::SUCCESS;
}

} // namespace

void register_init(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    struct Args {
        std::string directory;
    };
    auto args = std::make_shared<Args>();

    auto* cmd_init = app.add_subcommand("init", "Initialize .sele/");
    cmd_init->add_option("--root", global_opts.root, "Target directory to initialize (default: current directory)");
    add_output_opt(cmd_init, global_opts);
    cmd_init->add_option("directory", args->directory, "Target directory (overrides --root)");
    cmd_init->footer("Example:\n  sele init\n  sele init ./my-project");
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

            init_next(target, output, exit_code);
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
