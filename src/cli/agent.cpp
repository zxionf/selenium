#include "cli/common.hpp"

#include "cli/commands.hpp"
#include "core/entity.hpp"
#include "core/fs.hpp"
#include "core/store.hpp"

#include <nlohmann/json.hpp>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string context_value(const core::store::Paths& paths, const char* key) {
    if (!core::fsys::exists(paths.context)) return "";
    try {
        return nlohmann::json::parse(core::fsys::read_file(paths.context)).value(key, "");
    } catch (...) {
        return "";
    }
}

struct AgentArgs {
    std::string plan;
    std::string line;
    std::string task;
};

} // namespace

void register_agent_cmd(CLI::App& app, GlobalOptions& global, int& exit_code) {
    auto args = std::make_shared<AgentArgs>();
    auto* cmd = app.add_subcommand("agent", "Print the agent constraint prompt");
    add_global_opts(cmd, global);
    cmd->add_option("--plan", args->plan, "Plan id");
    cmd->add_option("--line", args->line, "Line id");
    cmd->add_option("--task", args->task, "Task id");
    cmd->callback([&global, &exit_code, args]() {
        const auto ctx = make_context(global);
        (void)exit_code;
        const std::string plan_id = args->plan.empty() ? context_value(ctx->paths, "plan")
                                                       : args->plan;
        const std::string line_id = args->line.empty() ? context_value(ctx->paths, "line")
                                                       : args->line;

        std::vector<core::model::Task> tasks;
        try {
            tasks = core::store::load_tasks(ctx->paths);
        } catch (const std::exception& error) {
            fail(*ctx, exit_code, exit_codes::ENV_ERROR, error.what());
            return;
        }

        const core::model::Task* current = nullptr;
        for (const auto& task : tasks) {
            if (task.id != args->task && args->task.empty() && task.status != "in_progress") {
                continue;
            }
            if (!args->task.empty() && task.id != args->task) continue;
            if (!plan_id.empty() && task.plan != plan_id) continue;
            if (!line_id.empty() && task.line != line_id) continue;
            current = &task;
            break;
        }

        std::ostringstream text;
        text << "你是 sele 的执行 agent。\n\n";
        if (!plan_id.empty()) {
            const auto path = core::store::plan_path(ctx->paths, plan_id);
            if (core::fsys::exists(path)) {
                try {
                    const auto plan = core::model::plan_from_json(
                        nlohmann::json::parse(core::fsys::read_file(path)));
                    text << "计划: " << plan.id << "（目标: " << plan.goal << "）\n";
                } catch (...) {
                }
            }
        }
        if (!line_id.empty()) text << "当前线: " << line_id << "\n";
        if (current != nullptr) {
            text << "当前任务: " << current->id << " " << current->title << " (uid: "
                 << current->uid << ")\n";
            text << "任务验收策略: " << current->review_policy << "\n";
            if (!current->current_step.empty()) {
                text << "当前步骤: " << current->current_step << "\n";
            }
            text << "\n可改模块:";
            for (const auto& module : current->modules) text << " " << module;
            text << "\n\n操作规则:\n";
            text << "- 完成步骤: sele step done " << current->id << " <step>\n";
            text << "- 提交代码必须经: sele git commit --task " << current->id
                 << " --step <step>\n";
            text << "- 不可直接执行 git commit / git reset / git push\n";
            text << "- 岔路决策记录: sele decision record \"<内容>\" --task " << current->id
                 << "\n";
            text << "- 需要切换任务: sele task pause " << current->id
                 << " --reason \"<原因>\"\n";
        } else {
            text << "当前没有 in_progress 任务；用 sele next 查看就绪任务。\n";
        }
        text << "\n禁止: 修改 plan/line 定义、创建新 task、跳过提交\n";
        ctx->out.print_both(text.str(), {{"command", "agent"}, {"prompt", text.str()}});
    });
}
