# AGENTS.md

`sele`：管理 `.sele/` 四层任务体系的 C++20 CLI（Plan → Line → Task → Step）。
当前版本 **0.21.3**，**只支持当前 `.sele` 结构**：结构不匹配会被直接拒绝（退出码 4）。

## 入口

1. **agent 契约：`.sele/CONTRACT.md`（不可违反清单，优先阅读）**。
2. 操作手册：`docs/sele-0.21.3-agent.md`。
3. 接口规范：`docs/sele-f.md`；概念语义：`docs/sele-f-d.md`。
4. 当前状态：`sele status --output json`、`sele next --output json`、`sele task`、`sele step`。
5. `.sele/` 是工具私有目录，只通过 `sele` 命令读写；不要手改。

## 体系

```
Plan → Line → Task（内嵌于 line.json 的 tasks）→ Step（内嵌于 task）
```

- 任务全部内嵌于 `line.json`；不做回滚。
- **I1**：全局至多一个 `in_progress` task；`task start` 会自动 pause 当前 task。
- **I2**：单 task 内至多一个 `in_progress` step。
- 所有迁移写 `log.jsonl`（append-only）。

## 最小闭环

```bash
sele init

sele plan new <plan> --goal "..." --acceptance "..." --lines <line>
sele plan submit <plan>
sele plan approve <plan> --by human:<name>

sele line new <line> --plan <plan> --goal "..." --acceptance "..." --modules a,b
sele line switch <line> --plan <plan>

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

1. 状态迁移必须由显式命令驱动；禁止手改 `.sele/` 下的 JSON。
2. `log.jsonl` 是 append-only 审计记录；`log compact` 只归档，不删除。
3. I1 / I2 如上；`task done` 前所有 step 必须是 `done`/`skipped`。
4. `task done` 未满足 review_policy 时转 `pending_review`；`line done` / `plan done` 要求所有下层结束且本层验收通过。
5. **git 只读**：只查询（`status`/`diff`/`log`/`show` 等），禁止任何 git 写命令；`sele git` 写命令（`commit`/`stash`/`branch create`/`branch switch`/`reconcile`）执行前必须把命令、`git status --short`、`git diff --stat` 和影响报给用户，获授权后才执行。
6. 提交频率：一个 task 最多一次提交，在 task 完成时提交；小修合并，不逐 step 提交。
7. 不做回滚：需要回滚代码时明确告知用户，由用户决定。
8. 不改 plan/line 定义、不创建 task（这是人的权限）；需要变更时 `sele decision record` 并请求用户。
9. 禁止 python/nodejs 脚本；改文件用 `edit`/`write`；校验脚本经 stdin 就地执行；思考与输出用中文。
10. 检测到不支持的 `.sele` 结构时：删除 `.sele/` 后重新 `sele init`，不要迁移。

## 视图

```bash
sele task            # 当前 line 的 task（依赖拓扑序；状态显示 active/review）
sele task --all      # 当前 plan 的所有 line
sele task --global   # 所有 plan
sele step            # 当前 task 的 step（数组顺序）
sele step --all      # 当前 line 的所有 task 的 step
sele step --global   # 所有 plan 的 step
sele next / status / tree / log / check
sele agent           # 当前任务约束提示词
```

## 验证

```bash
cmake -B build -S .
cmake --build build
bash tests/sele_v2_tests.sh build/build/sele
ctest --test-dir build --output-on-failure
```

构建开启 `-Wall -Wextra -Wpedantic -Werror`，警告即错误。
