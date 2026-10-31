# Sele 规范 v2.0

## 一、定位

**Sele 是约束型 Agent 的任务编排与进度账本。**

- **第一用户是 Agent**：Sele 让 agent 知道「要做什么、做到哪、怎么切回、下一步做什么」。
- **第二用户是开发者**：用 Sele 做计划制订、审批、验收、审计、归档。
- **不做**：推理、规划、Agent 本体、CI/CD、多机调度、质量评判。
- **只保证**：产出可审计、进度可恢复、状态可追溯。
- **借鉴 Git**：节点化、可追溯、可追溯、扁平存储。不套用分支合并模型。
- **Git 关系**：Agent **永不直接操作 Git 写操作**，所有写操作经 Sele 代理。Sele 承担一致性责任。

## 二、设计原则

| # | 原则 |
|---|---|
| P1 | 单 Agent、单 Task、轮换执行，无物理并发 |
| P2 | 线内任务默认串行；并行是逻辑并行（挂起轮换），非物理并发 |
| P3 | Step 是 Task 的待办事项，内嵌于 Task，顺序即执行顺序，支持跳过与跳转 |
| P4 | 所有状态迁移必须带 `reason`，即决策记录 |
| P5 | 任何 Task 完成必须留下 `task.done` 事件 |
| P6 | 状态变更必须由显式命令驱动，禁止后台静默推进 |
| P7 | Agent 对 Git 只读自由，写操作必须经 `sele git *` 代理 |
| P8 | Sele 不能阻止外部 Git 操作，但必须检测漂移并给出一致化路径 |
| P9 | 审计日志 append-only，不可修改 |
| P10 | 存储格式为 JSON |
| P11 | 实体以类型扁平存储；Task 用 uid hash 命名，分桶；Line 短名带 plan 前缀 |


## 三、概念模型

### 3.1 四层结构

```
Plan          计划
 └ Line       线
    └ Task    任务（功能节点：内嵌多 Step + 多验收点）
       └ Step 步骤（内嵌于 Task）
```

**无退化**：所有 Plan 一律含至少一条 Line；单线场景显式创建 Line `main`，不设 `_default` 隐式 Plan。

### 3.2 归属约束

- 一个 Line 只归属一个 Plan
- 一个 Task 只归属一个 Line
- 一个 Step 只归属一个 Task（物理内嵌，天然约束）

### 3.3 线间依赖（动态）

`line.depends` 支持两种引用：

```json
{
  "depends": [
    {"type": "line", "id": "auth-core"},
    {"type": "task", "line": "session-mgmt", "id": "T5"}
  ]
}
```

依赖可动态增删，变更时重算拓扑并检查环。


## 四、状态机

### 4.1 Plan

```
draft ──submit──> pending_approval ──approve──> active ──┬──> done ──archive──> archived
                      │                                  └──> aborted
                      └──reject──> draft
```

### 4.2 Line

```
draft ──activate──> active ──┬──> done
                             ├──> blocked ──unblock──> active
                             └──> abandoned
```

### 4.3 Task

```
todo ──start──> in_progress ──┬──> done
                              ├──> pending_review ──approve──> done
                              │                    └reject──> in_progress
                              ├──> paused ──resume──> in_progress
                              ├──> blocked ──unblock──> in_progress
                              └──> failed ──retry──> in_progress

done ──archive──> archived ──revive──> todo（可追加新 step）
```

`paused` 与 `pending_review` 是一等状态。

### 4.4 Step（内嵌于 Task，独立状态字段）

```
todo ──start──> in_progress ──┬──> done
                              ├──> pending_review ──approve──> done
                              │                    └reject──> in_progress
                              ├──> blocked ──unblock──> in_progress
                              ├──> failed ──retry──> in_progress
                              └──> skipped（记原因）

任意 ──jump_to──> 目标 step（中间 step 标 skipped，记跳转）
```

**约束**：同一 Task 内，同一时间只有一个 Step 处于 `in_progress`。

### 4.5 迁移前置条件（核心）

| 迁移 | 前置 |
|---|---|
| task.start | 所有 depends 的 task 为 done；无其他 in_progress task |
| task.done | 所有 step 为 done/skipped；所有 acceptance 满足 review_policy |
| task.pending_review→done | 人工 approve |
| step.start | 无其他 in_progress step；前置 step 非 todo |
| plan.done | 所有 line/task done；计划级 acceptance 通过 |
| plan.archive | done 状态 + 显式确认 |


## 五、数据结构

### 5.1 Plan

```json
{
  "id": "login-system",
  "goal": "用户能用邮箱密码登录并获得 session",
  "acceptance": [
    {"id": "PA1", "desc": "登录端到端可用", "status": "unverified"},
    {"id": "PA2", "desc": "错误处理不泄露用户存在性", "status": "unverified"}
  ],
  "lines": [
    {"id": "auth-core", "depends": []},
    {"id": "session-mgmt", "depends": [{"type": "line", "id": "auth-core"}]}
  ],
  "status": "active",
  "created_at": "2026-10-30T10:00:00Z",
  "approved_by": "human:alice",
  "approved_at": "2026-10-30T10:30:00Z"
}
```

**Plan 是 manifest 索引源**：归档时依 `lines` 字段聚合。

### 5.2 Line

```json
{
  "id": "auth-core",
  "plan": "login-system",
  "goal": "提供基础认证能力",
  "acceptance": [
    {"id": "A1", "desc": "登录接口可用"}
  ],
  "modules": ["auth", "token"],
  "depends": [],
  "status": "active",
  "created_at": "2026-10-30T10:00:00Z"
}
```

### 5.3 Task（Step 内嵌）

```json
{
  "uid": "tsk_01H2XABCDEFGHJKMNPQRSTVWXY",
  "id": "T1",
  "plan": "login-system",
  "line": "auth-core",
  "title": "实现 /login API",
  "goal": "提供可用的登录接口",
  "status": "in_progress",
  "modules": ["auth", "token"],
  "depends": [
    {"type": "task", "plan": "login-system", "line": "auth-core", "id": "T0"}
  ],
  "review_policy": "mixed",
  "acceptance": [
    {"id": "A1", "desc": "成功返回 token", "status": "pass", "verified_by": "human:alice"},
    {"id": "A2", "desc": "错误密码 401", "status": "unverified"}
  ],
  "steps": [
    {"id": "S1", "title": "设计 schema", "status": "done", "requires_review": false, "depends": []},
    {"id": "S2", "title": "实现 refresh 接口", "status": "in_progress", "requires_review": false, "depends": []},
    {"id": "S3", "title": "端到端测试", "status": "todo", "requires_review": true, "depends": []}
  ],
  "current_step": "S2",
  "pause_context": null,
  "locked": {"locked": false, "by": null, "type": null},
  "archived_at": null,
  "archived_git_head": null,
  "created_at": "2026-10-30T10:00:00Z",
  "finished_at": null
}
```

**关键字段**：

| 字段 | 说明 |
|---|---|
| `uid` | 不可变，ULID 格式。Task 文件名 hash 基于此，改名/换 line 不影响 |
| `id/plan/line` | 可变语义 ID，仅 JSON 内部与索引使用 |
| `steps` | 内嵌数组，顺序即执行顺序 |
| `current_step` | 当前 step id |
| `pause_context` | 暂停现场快照（见 6.1） |
| `review_policy` | `strict` / `lenient` / `mixed`（默认） |

**`review_policy`**：

| 值 | 含义 |
|---|---|
| `strict` | 所有 acceptance 须 `verified_by: human:*` 才可 done |
| `lenient` | `verified_by: agent:*` 即可 done，人工异步复审 |
| `mixed` | 标 `requires_human: true` 的 acceptance 须人工验，其余自验 |

**Step 字段**（内嵌于 `steps` 数组）：

| 字段 | 说明 |
|---|---|
| `id` | Step 短 ID（`S1`），Task 内唯一 |
| `title` / `desc` | 可改（记录事件） |
| `status` | 七态见 4.4 |
| `depends` | 默认为空数组（v0.1 不做 step 依赖，保留字段） |
| `requires_review` | 完成后是否转 `pending_review` |
| `locked` | 人工/依赖锁 |
| `skipped_reason` / `jump_meta` | 跳转/跳过记录 |

### 5.5 事件日志

append-only JSONL，条目：

```json
{
  "ts": "2026-10-30T11:30:00Z",
  "actor": "agent|human:alice",
  "op": "step.done",
  "entity": {
    "type": "step",
    "plan": "login-system",
    "line": "auth-core",
    "task": "T1",
    "step": "S2"
  },
  "before": "in_progress",
  "after": "done",
  "reason": "实现完成并通过本地测试",
  "context": {"files": ["src/auth/token.py"]}
}
```

**决策记录**即 `op: decision`：

```json
{
  "ts": "...",
  "actor": "agent",
  "op": "decision",
  "entity": {"type": "task", "plan": "login-system", "line": "auth-core", "task_id": "T1"},
  "reason": "选择 JWT 而非 session cookie，因需支持移动端",
  "context": {"alternatives": ["session", "jwt"], "chosen": "jwt"}
}
```

`sele log --decisions` 可单独筛选。


## 六、核心机制

### 6.1 暂停与恢复

**触发**：

- 主动：`sele task pause T1 --reason "切换到 T9"`
- 被动：`sele task start T9` 时若 T1 为 `in_progress`，自动暂停（配置 `auto_pause_on_switch: true`）

**pause_context**：

```json
{
  "at": "2026-10-30T12:00:00Z",
  "reason": "切换到 T9",
  "current_step": "S2",
  "step_progress": "写完 2/4 接口",
  "git_snapshot": {
    "head": "def5678",
    "branch": "main",
    "dirty_files": ["src/auth/login.py"],
    "diff_stat": "1 file, +20/-5"
  },
  "working_files": ["src/auth/login.py"],
  "open_questions": ["token 过期策略待定"],
  "resume_hint": "从 refresh 接口继续，先补 logout",
  "skipped_count": 0
}
```

**恢复**：`sele task resume T1`，Sele 提示工作区对比，**不决策**：

```
恢复 T1 (暂停于 12:00，期间跳过 2 个 task)
上次工作区脏文件: src/auth/login.py
当前 HEAD: 9ab0123（与暂停时不同）
当前工作区: clean
提示: 检查 T9 产出是否影响 T1 工作文件
```

### 6.2 跳过计数与警告

**不按时间计算**。每次 `sele next` 或 `sele task start` 时，未选中 paused task 则 `skipped_count += 1`。

```json
{
  "pause_policy": {
    "warn_after_skips": 3,
    "escalate_after_skips": 5,
    "auto_action": "none"
  }
}
```

- `>= warn`：`status` 标黄
- `>= escalate`：标红，提示开发者介入
- 开发者决策：`archive` / `abandon` / `move --to-line` / `keep`（重置计数）

### 6.3 动态插入任务

```bash
sele task insert "紧急修复登录崩溃" --after T1 --pause-current
```

规则：

- 必须指定锚点（`--after` / `--before`）
- `--pause-current`：若当前 task `in_progress`，自动 pause
- 已完成的 task 不动
- 插入后触发 `sele line check` 重算依赖

### 6.4 Step 操作

| 操作 | 命令 | 约束 |
|---|---|---|
| 新增 | `sele step add T1 "标题" [--after S2]` | 默认追加末尾 |
| 修改 | `sele step update T1 S2 --title/--desc/--requires-review` | 记录事件 |
| 删除 | `sele step delete T1 S2 --reason` | 仅 `todo`/`skipped`；`done` 不可删 |
| 重排 | `sele step move T1 S3 --after S1` | 仅 `todo` 可移 |
| 锁定 | `sele step lock T1 S2 --type manual --reason` | 锁后禁改/删/移 |
| 开始 | `sele step start T1 S2` | 无其他 in_progress |
| 完成 | `sele step done T1 S2` | 若 `requires_review` → `pending_review` |
| 跳过 | `sele step skip T1 S2 --reason` | → `skipped` |
| 跳转 | `sele step goto T1 S4 --reason` | 中间 step 标 `skipped` |

### 6.5 人工审核

**Step 级**：`requires_review: true` 的 step 完成 → `pending_review` → 人工 `sele step approve` → `done`。

**Task 级**：由 `review_policy` 控制。所有 step 完成后：

- `strict` / `mixed` 中需人工验的 acceptance 未通过 → `pending_review`
- `lenient` 全自验通过 → 直接 `done`

验收点标记：

```bash
sele task verify T1 A1 --by agent
sele task verify T1 A1 --by human:alice
```

### 6.6 任务归档与复活

**归档**：仅 `done` 状态。记录 `archived_at` 与 `archived_git_head`。旧 step 全部只读保留。

**复活**：`sele task revive T1 [--base current|archived]`

- `current`（默认）：回到 `todo`，在当前 HEAD 追加新 step，旧 step 只读
- `archived`：Sele 提示 `git diff <archived>..<current>` 供人查看代码漂移，**不自动 checkout**

### 6.7 导航：sele next

**纯拓扑序，不考虑时间与优先级**。

```bash
sele next
```

返回所有**就绪** task（状态 `todo` 或 `paused`，且所有 depends 为 `done`），按拓扑序：

```
就绪任务（拓扑序）：
  1. T5 (line: session-mgmt) - 依赖已满足
  2. T7 (line: login-ui) - 依赖已满足
  3. T9 (line: e2e-tests) - 依赖部分满足
```

`--line <id>` 限定线内；`--resume` 优先列 paused。


## 七、目录结构与命名

### 7.1 目录

```
.sele/
├── config.json
├── plans/
│   └── <plan-id>.json
├── lines/
│   └── <plan-id>.<line-id>.json
├── tasks/
│   └── <hash[:2]>/
│       └── <hash[2:]>.json
├── log.jsonl
├── archive/
│   └── <plan-id>-<ts>/
│       ├── _manifest.json
│       ├── plans/
│       ├── lines/
│       └── tasks/
└── cache/
    ├── git-fingerprint.json
    └── index.json
```

### 7.2 Task 文件命名

```
hash = sha256(uid)[:16]
路径 = tasks/<hash[:2]>/<hash[2:]>.json
```

- 基于不可变 `uid`（ULID），改名/换 plan/换 line **文件名不变**
- 前 2 位分桶，万级 task 每桶 ≤40 文件
- 语义 ID 只在 JSON 内和索引中

### 7.3 Line 文件命名

```
lines/<plan-id>.<line-id>.json
```

- 物理带 plan 前缀，避免跨 plan 重名冲突
- CLI 层支持简写：当前上下文（`sele line switch`）下 `sele line switch auth-core` 自动补全 plan

### 7.4 ID 约束

**所有语义 ID**（plan、line、task id、step id）：

```regex
^[a-zA-Z0-9][a-zA-Z0-9_-]{0,40}$
```

- 允许：字母、数字、下划线、连字符
- **禁止**：点号（文件名分隔符）、斜杠、空格
- 长度：1–41

**uid**：ULID 格式（26 字符 Crockford Base32），由 sele 生成，不可修改。

### 7.5 索引缓存

`cache/index.json`：

```json
{
  "version": 1,
  "tasks": {
    "login-system.auth-core.T1": {
      "uid": "tsk_01H2XABCDEFGHJKMNPQRSTVWXY",
      "file": "3a/f9b2c1d4e5f6a7b8"
    }
  },
  "lines": {
    "login-system.auth-core": {"file": "login-system.auth-core.json"}
  },
  "plans": {
    "login-system": {"file": "login-system.json"}
  }
}
```

- **缓存，不纳入 git**
- 丢失可 `sele cache rebuild`：全扫描 task JSON 重建
- 结构性变更（创建 task/line/plan）后更新

### 7.6 归档

`sele plan archive login-system` 流程：

```
1. 读 plan.json 的 lines 字段
2. 依赖索引定位所有 line/task 文件
3. 建 archive/login-system-<ts>/，按类型建子目录
4. 逐个 mv
5. 写 archive/login-system-<ts>/_manifest.json（原始路径清单）
6. 写 log 事件 plan.archive.start / done
```

**若 `.sele/` 在 git 版本控制下**，归档作为一次 commit，天然原子；中断可 reset 到归档前 commit 重做。

**恢复**：`sele plan restore login-system --from <ts>` 反向移动。

### 7.7 版本控制

**纳入 git**：`config.json`、`plans/`、`lines/`、`tasks/`、`log.jsonl`、`archive/`

**排除**：`cache/`、`*.lock`


## 八、CLI 命令全集

### 8.1 Plan

```
sele plan new <id> --goal <str> [--acceptance <desc>]... --lines <id>[,<id>...]
sele plan submit <id>
sele plan approve <id> [--reason]
sele plan reject <id> --reason
sele plan status <id>
sele plan done <id>
sele plan archive <id>
sele plan restore <id> --from <ts>
sele plan list [--status]
```

### 8.2 Line

```
sele line new <id> --plan <plan-id> --goal <str> [--acceptance]... --modules a,b
sele line switch <id>                               # 支持简写，当前 plan 上下文补全
sele line status <id>
sele line check <id>
sele line dep add <id> --type line|task --target <ref>
sele line dep remove <id> --target <ref>
sele line done <id>
sele line abandon <id> --reason
sele line list [--plan] [--status]
```

### 8.3 Task

```
sele task add "<title>" --line <id> [--modules a,b] [--depends <ref>]... [--review-policy strict|lenient|mixed]
sele task insert "<title>" --after <task> [--pause-current]
sele task start <id>
sele task pause <id> --reason
sele task resume <id>
sele task block <id> --reason
sele task unblock <id>
sele task done <id>
sele task fail <id> --reason
sele task retry <id>
sele task verify <id> <acceptance-id> --by agent|human:<name>
sele task approve <id>
sele task reject <id> --reason
sele task archive <id>
sele task revive <id> [--base current|archived]
sele task move <id> --to-line <line>
sele task update <id> --modules ... --review-policy ...
sele task locate <id>                              # 语义 ID → 文件路径
sele task status <id>
sele task list [--line] [--status] [--all]
```

### 8.4 Step

```
sele step add <task> "<title>" [--after <step>] [--requires-review]
sele step update <task> <step> [--title] [--desc] [--requires-review]
sele step delete <task> <step> --reason
sele step move <task> <step> --after <step>
sele step lock <task> <step> --type manual|dependency --reason
sele step unlock <task> <step>
sele step start <task> <step>
sele step done <task> <step>
sele step fail <task> <step> --reason
sele step block <task> <step> --reason
sele step unblock <task> <step>
sele step skip <task> <step> --reason
sele step goto <task> <step> --reason
sele step approve <task> <step>
sele step reject <task> <step> --reason
sele step list <task>
```

### 8.5 导航与查询

```
sele next [--line] [--resume]
sele status [--with-git]
sele log [--line] [--task] [--step] [--since] [--decisions] [--git-commit]
sele decision record "<content>" [--line] [--task] [--step]
sele tree [--plan]                                 # 逻辑树视图（人类浏览）
sele check                                         # 全量一致性校验
sele cache rebuild
```

### 8.6 Git 代理

```
sele git commit --task <id> --step <id> --message "<msg>" [--files a,b]
sele git stash --task <id> [--message]
sele git stash pop --task <id>
sele git branch create <name> [--task]
sele git branch switch <name>
sele git status
sele git diff [--task]
sele git log [--task] [--since]
sele git reset --hard <sha> --confirm            # 仅 developer
sele git reconcile [--sync-to <sha> | --restore <sha>]
sele git health
```


## 九、Git 协作协议

### 9.1 分层权限

| 命令类别 | Agent | Developer | Sele 校验 |
|---|---|---|---|
| 只读（status/diff/log/show） | 直接用 | 直接用 | 无 |
| 提交（commit） | 经 sele 代理 | 经 sele 代理（推荐） | 强 |
| 暂存（stash/pop） | 经 sele 代理 | 经 sele 代理 | 中 |
| 分支（create/switch） | 受限 | 可 | 强 |
| 破坏（reset/rebase/push） | **禁止** | 仅手动或 `sele git reset --confirm` | 强 + 二次确认 |

**Sele 永不自动执行**：`rebase`、`push`、`force-push`、`gc`。

### 9.2 提交流程

```bash
sele git commit --task T1 --step S2 --message "实现 token 刷新" --files src/auth/token.py
```

Sele 校验：

1. task 状态 = `in_progress`，step 状态 = `in_progress`
2. files 是否落在 task.modules 范围内（`module_scope: off|warn|block`，默认 `warn`）
3. message 格式符合约定
4. 工作区无冲突标记

通过后执行 `git add <files> && git commit`。

**Message 格式**（`message_convention: sele`）：

```
sele(T1/S2): 实现 token 刷新

[sele-meta]
task: T1
step: S2
line: auth-core
plan: login-system
```

### 9.3 漂移检测

每次 Sele 命令启动对比 `git rev-parse HEAD` 与指纹：

| # | 类型 | 检测 | 默认响应 |
|---|---|---|---|
| D1 | HEAD 前进 | 旧 HEAD 是新 HEAD 祖先 | `auto_update` |
| D2 | HEAD 回退 | 新 HEAD 是旧 HEAD 祖先 | `block` |
| D3 | HEAD 分叉 | 无祖先关系 | `block` |
| D5 | Commit 被改写 | tree hash 不匹配 | `mark_stale` |
| D6 | 分支切换 | `branch --show-current` 变化 | `warn` |

`block` 时写操作被拒，返回：

```
Git 漂移：HEAD 从 abc1234 回退到 def5678
请运行: sele git reconcile
```

### 9.4 Reconcile

```bash
sele git reconcile --sync-to def5678
```

流程：

1. 展示漂移报告
2. 确认后用 `--sync-to <sha>` 更新指纹
3. 写 reconcile 事件

### 9.5 配置

```json
{
  "git": {
    "mode": "read",
    "gateway": {
      "require_active_task": true,
      "require_step": true,
      "module_scope": "warn",
      "message_convention": "sele",
      "allow_agent_checkout": false,
      "allow_agent_stash": true
    },
    "drift": {
      "on_d1_advance": "auto_update",
      "on_d2_rewind": "block",
      "on_d3_diverge": "block",
      "on_d4_missing": "mark_orphaned",
      "on_d5_rewritten": "mark_stale",
      "on_d6_branch": "warn"
    },
    "pause_strategy": "advisory"
  },
  "pause_policy": {
    "warn_after_skips": 3,
    "escalate_after_skips": 5,
    "auto_action": "none"
  }
}
```


## 十、一致性校验（`sele check`）

| 检查 | 说明 |
|---|---|
| C1 | 文件名格式合法（hash 长度、line 前缀） |
| C2 | 文件名 ↔ JSON 字段一致（task uid hash、line plan 前缀） |
| C3 | ID 字符集合法（禁 `.`） |
| C4 | 父实体存在（line 的 plan、task 的 line） |
| C5 | 无孤儿实体 |
| C6 | Plan 至少一条 Line |
| C7 | 索引与磁盘一致（可 `cache rebuild` 修复） |
| C8 | 归档目录不重叠 |
| C9 | 依赖图无环 |
| C10 | 验收可达性（Plan acceptance 有对应 task 覆盖） |


## 十一、Agent 约束注入

Agent 启动时注入：

```
你是 sele 的执行 agent。

计划: login-system（目标: 用户能用邮箱密码登录并获得 session）
当前线: auth-core
当前任务: T1 实现 /login API (uid: tsk_01H2X...)
任务验收策略: mixed
当前步骤: S2 实现 refresh 接口

可改模块: auth, token
当前步骤验收: 需人工审核

操作规则:
- 完成步骤: sele step done T1 S2
- 提交代码必须经: sele git commit --task T1 --step S2
- 不可直接执行 git commit / git reset / git push
- 岔路决策记录: sele decision record "<内容>" --task T1
- 需要切换任务: sele task pause T1 --reason "<原因>"

禁止: 修改 plan/line 定义、创建新 task、跳过提交
```


## 十二、默认决策汇总

| # | 决策 | 默认值 |
|---|---|---|
| 1 | Agent 只读 Git | 直接使用，不经代理 |
| 2 | `module_scope` | `warn` |
| 4 | 漂移 D2/D3 | `block` |
| 5 | `log.jsonl` 进 Git | 是，支持 compact |
| 7 | Agent stash | 允许，须关联 task |
| 8 | 归档旧 step | 只读保留，新 step 纯追加 |
| 9 | 线归属 | 唯一 Plan；Plan 恒含至少一条 Line，轻量用显式 `main` |
| 10 | `sele next` 排序 | 纯拓扑序 |
| 11 | Task 文件命名 | `sha256(uid)[:16]`，前 2 位分桶 |
| 12 | Line 文件命名 | `<plan>.<line>.json`，CLI 可简写 |
| 13 | 索引 | `cache/index.json`，不纳入 git，可重建 |


## 十三、版本规划

### v0.1

- 四层结构 + 状态机
- JSON 存储 + 事件日志
- Task uid hash 命名 + 分桶 + 索引缓存
- 暂停/恢复 + 跳过计数
- Step 内嵌 + 顺序/跳过/跳转/锁
- 人工验收（strict/lenient/mixed）
- Task 归档/复活
- `sele next` 拓扑导航
- Git 代理提交 + 漂移检测 + reconcile
- CLI 全集

### v0.2+

- Plan 级审批流强化
- `sele tree` 图形化
- 跨仓库支持
- 多 Plan 视图