#include "cli/commands.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cli/nextgen.hpp"
#include "core/fs.hpp"
#include "core/gitrt.hpp"
#include "core/sele.hpp"
#include "util/exit_codes.hpp"
#include "util/output.hpp"

namespace fs = std::filesystem;

namespace cli {

namespace {

using ng::fail;

fs::path worktree_dir(const core::sele::Layout& layout) {
    return layout.runtime / "worktrees";
}

fs::path pointer_file(const core::sele::Layout& layout, const std::string& line_id) {
    return worktree_dir(layout) / (line_id + ".json");
}

std::string default_path(const fs::path& root, const std::string& line_id) {
    const std::string name = root.filename().string() + "-wt-" + line_id;
    return (root.parent_path() / name).string();
}

void write_pointer(const core::sele::Layout& layout, const std::string& line_id,
                   const std::string& branch, const std::string& path) {
    core::fsys::ensure_dir(worktree_dir(layout));
    core::sele::json value = core::sele::json::object();
    value["schema_version"] = 1;
    value["line"] = line_id;
    value["branch"] = branch;
    value["path"] = path;
    value["created_at"] = core::sele::utc_now();
    core::sele::write_json(pointer_file(layout, line_id), value);
}

} // namespace

void register_worktree(CLI::App& app, GlobalOptions& global_opts, int& exit_code) {
    auto* group = app.add_subcommand("worktree", "Manage per-line git worktrees");
    group->require_subcommand(1);

    struct AddArgs {
        std::string id;
        std::string path;
    };
    auto add_args = std::make_shared<AddArgs>();
    auto* cmd_add = group->add_subcommand("add", "Create a worktree for a line branch");
    add_global_opts(cmd_add, global_opts);
    cmd_add->add_option("line", add_args->id, "Line id")->required();
    cmd_add->add_option("--path", add_args->path,
                        "Target directory (default: <repo>-wt-<line-id>)");
    cmd_add->footer("Example:\n  sele worktree add 20261029-fix-login\n"
                    "  sele worktree add 20261029-fix-login --path ../wt-login");
    cmd_add->callback([&global_opts, &exit_code, add_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            if (!core::sele::valid_line_id(add_args->id)) {
                fail(exit_codes::USAGE_ERROR, "Invalid line id: " + add_args->id, "",
                     {"sele line list"});
            }
            if (!core::gitrt::is_repo(ctx.lay.root)) {
                fail(exit_codes::ENV_ERROR, "Not a git repository: " + ctx.lay.root.string(), "");
            }

            const std::string branch = "sele/" + add_args->id;
            if (!core::gitrt::branch_exists(ctx.lay.root, branch)) {
                fail(exit_codes::STATE_CONFLICT, "Line branch not found: " + branch,
                     "Open the line to create its branch",
                     {"sele line open " + add_args->id});
            }
            if (core::gitrt::current_branch(ctx.lay.root) == branch) {
                fail(exit_codes::STATE_CONFLICT,
                     "Branch " + branch + " is already checked out in this worktree",
                     "Switch this worktree to another branch first",
                     {"git switch main", "sele worktree add " + add_args->id});
            }

            const std::string target = add_args->path.empty()
                                           ? default_path(ctx.lay.root, add_args->id)
                                           : add_args->path;
            if (core::fsys::exists(target)) {
                fail(exit_codes::STATE_CONFLICT, "Target already exists: " + target,
                     "Choose another --path or remove it");
            }

            const auto result =
                core::gitrt::run(ctx.lay.root, {"worktree", "add", target, branch});
            if (result.code != 0) {
                fail(exit_codes::ENV_ERROR, "git worktree add failed: " + result.out, "");
            }
            write_pointer(ctx.lay, add_args->id, branch, target);

            nlohmann::json response;
            response["command"] = "worktree.add";
            response["changed"] = true;
            response["line"] = add_args->id;
            response["branch"] = branch;
            response["path"] = target;
            output.print_both("Created worktree for " + add_args->id + " at " + target,
                              response);
        });
    });

    auto* cmd_list = group->add_subcommand("list", "List worktrees tracked by sele");
    add_global_opts(cmd_list, global_opts);
    cmd_list->footer("Example:\n  sele worktree list");
    cmd_list->callback([&global_opts, &exit_code]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            nlohmann::json items = nlohmann::json::array();
            std::ostringstream text;
            int shown = 0;

            const auto directory = worktree_dir(ctx.lay);
            if (core::fsys::exists(directory)) {
                std::vector<fs::path> files;
                for (const auto& entry : fs::directory_iterator(directory)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".json") {
                        files.push_back(entry.path());
                    }
                }
                std::sort(files.begin(), files.end());
                for (const auto& file : files) {
                    const auto value = core::sele::read_json(file);
                    const std::string line_id = value.value("line", std::string());
                    const std::string branch = value.value("branch", std::string());
                    const std::string path = value.value("path", std::string());
                    const bool present = core::fsys::exists(path);

                    items.push_back({{"line", line_id},
                                     {"branch", branch},
                                     {"path", path},
                                     {"present", present}});
                    text << line_id << "  " << branch << "  " << path
                         << (present ? "" : "  (missing)") << '\n';
                    ++shown;
                }
            }
            if (shown == 0) text << "(no worktrees)";

            nlohmann::json response;
            response["command"] = "worktree.list";
            response["items"] = items;
            output.print_both(text.str(), response);
        });
    });

    struct RemoveArgs {
        std::string id;
        bool force = false;
    };
    auto remove_args = std::make_shared<RemoveArgs>();
    auto* cmd_remove = group->add_subcommand("remove", "Remove a line worktree");
    add_global_opts(cmd_remove, global_opts);
    cmd_remove->add_option("line", remove_args->id, "Line id")->required();
    cmd_remove->add_flag("--force", remove_args->force,
                         "Pass --force to git worktree remove (discard changes)");
    cmd_remove->footer("Example:\n  sele worktree remove 20261029-fix-login\n"
                       "  sele worktree remove 20261029-fix-login --force");
    cmd_remove->callback([&global_opts, &exit_code, remove_args]() {
        util::Output output(global_opts.output_format);
        exit_code = ng::handle(output, [&] {
            auto ctx = ng::load_root(global_opts);
            const auto pointer = pointer_file(ctx.lay, remove_args->id);
            if (!core::fsys::exists(pointer)) {
                fail(exit_codes::STATE_CONFLICT,
                     "No worktree recorded for line " + remove_args->id,
                     "List recorded worktrees", {"sele worktree list"});
            }
            const auto value = core::sele::read_json(pointer);
            const std::string path = value.value("path", std::string());
            if (path.empty()) {
                fail(exit_codes::ENV_ERROR, "Worktree pointer has no path: " + pointer.string(),
                     "");
            }

            std::vector<std::string> args = {"worktree", "remove"};
            if (remove_args->force) args.push_back("--force");
            args.push_back(path);
            const auto result = core::gitrt::run(ctx.lay.root, args);
            if (result.code != 0) {
                fail(exit_codes::ENV_ERROR,
                     "git worktree remove failed: " + result.out,
                     "Use --force only when the worktree has no needed changes");
            }
            std::error_code ignored;
            fs::remove(pointer, ignored);

            nlohmann::json response;
            response["command"] = "worktree.remove";
            response["changed"] = true;
            response["line"] = remove_args->id;
            response["path"] = path;
            response["forced"] = remove_args->force;
            output.print_both("Removed worktree for " + remove_args->id + " at " + path,
                              response);
        });
    });
}

} // namespace cli
