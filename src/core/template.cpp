#include "core/template.hpp"

namespace core {

std::string render_agents_template() {
    return R"(# AGENTS.md

project-name

本文档不允许 ai 修改；确需修改必须由用户发起。

## 入口

- **agent 契约：`.sele/CONTRACT.md`（不可违反清单，优先阅读）**。
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

std::string render_sele_contract_template() {
    return R"(# sele contract

本文档由 `sele init` 生成，是 agent 在 `.sele` 项目中的不可违反清单；需要修改时先与用户确认。

## 不可违反

- `.sele/` 是工具私有目录，只通过 `sele` 命令读写；不要手改 `line.json`、`tasks/*.json`、会话指针。
- `task done` 前必须有 `checkpoint`；`line done` 前所有 task 与 acceptance 必须完成。
- 验收只记账：用 `sele acceptance pass|fail --evidence`；不要伪造 evidence。
- 干净判定由工具执行（排除 `.sele/`）；不要用 stash 或手改绕过。
- 一条 line 只作用于本仓库；跨仓库工作请新建 line 或与用户确认。
- 多 agent 并行时用 `--session` 或 `worktree` 隔离，不要共用会话指针。
- `merged` / `abandoned` / archived 的 line 只读。
- 结构（状态、depends、group、勾选）只由命令写；散文（goal/desc/title）可自由写。

## 完成定义

- `sele line check <id>` 通过（任务图、冻结、模块）。
- 所有 task `done`，所有 acceptance `pass`。
- `sele line done <id>` 成功（默认提交元数据）。

## 入口

- 状态：`sele overview --output json`
- 命令：`sele --help`、`sele line --help`、`sele task --help`
)";
}

} // namespace core
