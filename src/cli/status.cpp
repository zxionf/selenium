#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "core/root.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

void register_status(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto* cmd_status = app.add_subcommand("status", "Show current active change status");
    add_global_opts(cmd_status, global_opts);
    cmd_status->footer("Example:\n  sele status --output json");
    cmd_status->callback([&global_opts, &exit_code]() {
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

            std::ostringstream text;
            text << "Active change: " << (active_id.empty() ? "none" : active_id) << '\n'
                 << "Tasks: " << result["tasks"]["done"].get<int>() << '/'
                 << result["tasks"]["total"].get<int>() << " done\n"
                 << "Decisions: " << result["decisions"].get<int>() << '\n'
                 << "Consistent: " << (result["consistent"].get<bool>() ? "yes" : "no");

            output.print_both(text.str(), result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
