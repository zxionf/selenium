# sele contract

> 本文档是 agent 在 `.sele` 项目中的不可违反清单。操作手册见
> `docs/sele-0.20.0-agent.md`；需要修改本文件时先与用户确认。

## 入口

- 状态：`sele status --output json`、`sele next --output json`
- 当前任务提示词：`sele agent`
- 一致性：`sele check`
- 命令：`sele --help`、`sele plan --help`、`sele line --help`、
  `sele task --help`、`sele step --help`、`sele git --help`

## 不可违反

### 数据

- `.sele/` 是工具私有目录，只通过 `sele` 命令读写；不要手改
  `plans/`、`lines/`、`log.jsonl`。
- `log.jsonl` 是 append-only 审计记录；`log compact` 只归档，不删除。
- `cache/` 是本地派生数据，可 `cache rebuild` 重建，不进版本控制。
- 结构（状态、depends、步骤、验收）只由命令写；goal/desc/title 等散文可写，
  但仍应通过命令落盘。

### 状态机

- 所有迁移必须由显式命令触发；没有后台推进。
- **I1**：全局至多一个 `in_progress` task；`task start` 会自动 pause 当前 task。
- **I2**：单 task 内至多一个 `in_progress` step。
- `task done` 前所有 step 必须是 `done`/`skipped`；`line done` 前所有 task 必须
  结束且线验收通过；`plan done` 前所有 line 必须结束且计划验收通过。
- 验收只记账：`task/line/plan verify`；不要伪造 `--by` 或证据。
- `pending_review` 不是完成态；必须 `approve` 或补充 verify 后再 done。

### Git

- 提交统一经 `sele git commit --task <id> --step <id> --message "<msg>"`；
  不要直接 `git commit`。
- 不使用 `git reset --hard` / `git rebase` / `git push`；漂移用
  `sele git reconcile`。
- 不跳过提交：在 step 边界用 `sele git commit` 留下代码证据。
- 暂存用 `sele git stash --task <id>`（自动排除 `.sele`）。

### 权限

- plan/line 的定义、创建 task 是人的权限；agent 只执行、只记录。
- 不修改 plan/line 的 goal、验收、modules；需要变更时记录
  `sele decision record` 并请求用户。
- 不创建 python/nodejs 脚本；校验脚本经 stdin 就地执行（如 `bash -s <<'SH'`）。

### 工具约束

- 改文件只能用 `edit`/`write` 工具；不要用命令行重写文件。
- 思考与输出使用简体中文（代码、路径、命令除外）。
- 任何写操作检查退出码；失败先看 `suggestion`/`next`。

## 完成定义

- 所有 task `done`。
- 所有验收点 `pass`（按 review_policy 由 agent 或 human 验证）。
- `line done` / `plan done` 成功。
- `sele check` 通过。
