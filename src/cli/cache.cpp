#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/fs.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>

namespace {

std::string relative_to(const std::filesystem::path& path,
                        const std::filesystem::path& base) {
    return std::filesystem::relative(path, base).generic_string();
}

} // namespace

void register_cache_cmds(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto* cache = app.add_subcommand("cache", "Manage the cache directory");
    add_global_opts(cache, global);

    auto* rebuild = cache->add_subcommand("rebuild", "Rebuild cache/index.json");
    add_global_opts(rebuild, global);
    rebuild->callback([&global, &exit_code]() {
        const auto ctx = make_context(global);
        try {
            nlohmann::json index = {{"version", 1},
                                    {"plans", nlohmann::json::object()},
                                    {"lines", nlohmann::json::object()},
                                    {"tasks", nlohmann::json::object()}};
            for (const auto& plan : core::store::load_plans(ctx->paths)) {
                index["plans"][plan.id] = {
                    {"file", relative_to(core::store::plan_path(ctx->paths, plan.id),
                                         ctx->paths.sele)}};
            }
            for (const auto& line : core::store::load_lines(ctx->paths)) {
                index["lines"][line.plan + "." + line.id] = {
                    {"file", relative_to(core::store::line_path(ctx->paths, line.plan, line.id),
                                         ctx->paths.sele)}};
            }
            for (const auto& task : core::store::load_tasks(ctx->paths)) {
                index["tasks"][task.plan + "." + task.line + "." + task.id] = {
                    {"uid", task.uid},
                    {"file", relative_to(core::store::task_path(ctx->paths, task.uid),
                                         ctx->paths.sele)}};
            }
            core::fsys::write_atomic(ctx->paths.cache / "index.json", index.dump(2) + "\n");
            ctx->out.print_both("Rebuilt cache/index.json",
                                {{"command", "cache.rebuild"},
                                 {"plans", index["plans"].size()},
                                 {"lines", index["lines"].size()},
                                 {"tasks", index["tasks"].size()}});
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
        }
    });
}
