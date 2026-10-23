#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "core/root.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

namespace {

// Read "Key: value" from STATE.md, matching the key only at the start of a
// line. Returns the trimmed value, or "" when the key is absent.
std::string state_field(const std::string& content, const std::string& key) {
    size_t pos = 0;
    while (pos < content.size()) {
        size_t line_end = content.find('\n', pos);
        if (line_end == std::string::npos) line_end = content.size();
        std::string line = content.substr(pos, line_end - pos);
        if (line.rfind(key, 0) == 0) {
            std::string value = line.substr(key.size());
            size_t begin = 0;
            while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) ++begin;
            size_t end = value.size();
            while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
            return value.substr(begin, end - begin);
        }
        pos = line_end + 1;
    }
    return "";
}

} // namespace

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
            std::string phase;
            std::string blocked;
            std::string next;
            if (fs::exists(state_md)) {
                std::ifstream in(state_md);
                std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                active_id = state_field(content, "Active Change:");
                phase = state_field(content, "Phase:");
                blocked = state_field(content, "Blocked:");
                next = state_field(content, "Next:");
                if (active_id == "无") active_id.clear();
            }

            nlohmann::json result;
            result["active_id"] = active_id;
            result["phase"] = phase;
            result["blocked"] = blocked;
            result["next"] = next;
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
            if (!phase.empty()) text << "\nPhase: " << phase;
            if (!blocked.empty()) text << "\nBlocked: " << blocked;
            if (!next.empty()) text << "\nNext: " << next;

            output.print_both(text.str(), result);
            exit_code = exit_codes::SUCCESS;
        } catch (const std::exception& e) {
            output.error(e.what(), "");
            exit_code = exit_codes::ENV_ERROR;
        }
    });
}

} // namespace cli
