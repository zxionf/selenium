#include "core/template.hpp"
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace core {

std::string render_design_index_template() {
    return "# Design Index\n\n"
           "<!-- 设计文档索引：列出 SPEC、changes 等入口 -->\n";
}

std::string render_spec_template() {
    return "# Spec\n\n"
           "## 目标\n"
           "<!-- 只写稳定的规格，不写过程性内容 -->\n\n"
           "## 约束\n\n";
}

std::string render_agent_index_template() {
    return "# Agent Index\n\n"
           "<!-- agent 工作区索引：列出契约、状态与任务入口 -->\n";
}

std::string render_contract_template() {
    return "# Contract\n\n"
           "<!-- 约定 agent 的行为边界与交付标准 -->\n";
}

std::string render_state_template() {
    return "# Agent State\n\nActive Change: 无\n";
}

std::string render_agents_template() {
    return R"(# AGENTS.md

project-name

本文档不允许 ai 修改；确需修改必须由用户发起。

## 入口

- 首次使用：`sele init`（创建 `.sele/`；若本文件缺失会一并生成）。
- 当前状态：`sele overview --output json`。
- `.sele/` 是工具私有目录，只通过 `sele` 命令读写；不要用编辑器直接修改。
- 命令细节：`sele --help`、`sele line --help`、`sele task --help`。

## 最小闭环

```bash
sele line new <slug> --goal "..." --acceptance "desc::check" --modules a,b
sele task add "..." --modules a,b
sele line open <id>
sele task start T1 && sele task checkpoint T1 --msg "..." && sele task done T1
sele acceptance pass A1 --evidence "..."
sele line done <id>
sele line merge <id> --confirm
```

规则：`task done` 前必须有 `checkpoint`；`line done` 前所有 task 与 acceptance 必须完成；`.sele/` 的结构只由命令写。
)";
}

// A design doc is folded into SPEC.md by matching "## " titles, so its
// sections are named after the SPEC concept they revise. Change-scoped
// narrative (why, migration) belongs in the preamble or DECISIONS.log.
std::string render_design_template(const std::string& id) {
    return "# " + id + "\n\n"
           "<!-- 为什么改、边界与不属于本次的范围 -->\n\n"
           "## <概念>\n"
           "<!-- 折叠进 SPEC.md 同名小节后的现状 -->\n";
}

std::string render_tasks_template(const std::string& id) {
    return "# " + id + " 任务清单\n\n"
           "- [ ] 步骤 1\n"
           "- [ ] 步骤 2\n";
}

std::string render_decisions_header() {
    return "# Decisions\n";
}

std::string render_decision_entry(int index, const std::string& change_id,
                                  const std::string& context,
                                  const std::string& decision,
                                  const std::string& consequences) {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm = *std::gmtime(&time_t);

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
    std::string timestamp = oss.str();

    return "## [" + std::to_string(index) + "] " + timestamp + "\n"
           "Change: " + change_id + "\n"
           "Context: " + context + "\n"
           "Decision: " + decision + "\n"
           "Consequences: " + consequences + "\n";
}

} // namespace core