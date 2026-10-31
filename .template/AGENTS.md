# AGENTS.md

本项目的任务体系由 `sele`（0.20）管理：**Plan → Line → Task → Step**。

## 入口

1. `.sele/CONTRACT.md`：不可违反清单，优先阅读。
2. `.sele/` 只通过 `sele` 命令读写；不要手改。
3. 状态：`sele status`、`sele next`、`sele agent`。
4. 操作手册：`docs/sele-0.20.0-agent.md`（项目内存在时）。

## 最小闭环

```bash
sele init

sele plan new <plan> --goal "..." --acceptance "..." --lines <line>
sele plan submit <plan>
sele plan approve <plan> --by human:<name>

sele line new <line> --plan <plan> --goal "..." --acceptance "..." --modules a,b

sele task add "<title>" --line <line> --acceptance "..."
sele task start T1 --line <line>
sele step add T1 "<title>" --line <line>
sele step start T1 S1 --line <line>
sele step done T1 S1 --line <line>

sele task verify T1 A1 --line <line> --by human:<name>
sele task done T1 --line <line>
sele line verify <line> A1 --plan <plan> --by human:<name>
sele line done <line> --plan <plan>
sele plan verify <plan> PA1 --by human:<name>
sele plan done <plan>
```

## 规则

1. 只经 `sele` 读写 `.sele/`；`log.jsonl` 是 append-only。
2. 显式迁移；I1（全局单 `in_progress` task）、I2（单 task 单 `in_progress` step）。
3. 提交经 `sele git commit --task <id> --step <id>`；不直接 `git commit` /
   `git reset` / `git push`。
4. `task done` 前 step 全 `done`/`skipped`；`line done` / `plan done` 有各自前置。
5. 验收只记账，不伪造 `--by`。
6. agent 不改 plan/line 定义、不创建 task；需要变更时用 `sele decision record`
   并请求用户。
7. 验证：`sele check` 通过。
8. 禁止 python/nodejs 脚本；改文件用 `edit`/`write`；校验脚本经 stdin 就地执行。
9. **绝不向后兼容**：pre-0.20 的 `.sele` 布局会被拒绝（退 4），不要迁移；删除 `.sele/` 后重新 `sele init`。

## 常用

```bash
sele next / status / tree / log / check
sele agent
sele git status / diff / log / commit / stash / branch / health / reconcile
sele cache rebuild
```
