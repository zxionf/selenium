# sele contract

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
- 本工具只支持 `.sele/`；`.design/` 与 `.agent/` 不再被识别。

## 完成定义

- `sele line check <id>` 通过（任务图、冻结、模块）。
- 所有 task `done`，所有 acceptance `pass`。
- `sele line done <id>` 成功（默认提交元数据）。

## 入口

- 状态：`sele overview --output json`
- 命令：`sele --help`、`sele line --help`、`sele task --help`
