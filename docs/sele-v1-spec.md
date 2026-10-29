# sele v0.10 规格（.sele 体系）

> 状态：已实现（v0.3 基础 + v0.4 merge/revert + v0.5 task revert + v0.6 rebase + v0.7 worktree + v0.8 log --events + v0.9 fork/join + v0.10 archive）  
> 版本：0.10.0  
> 定位：以 `.sele/` 作为工具私有状态目录（类比 `.git/`），用「线（line）→ 任务（task）→ 验收（acceptance）」的显式状态机替代 `.design/.agent` 的自由文本状态。
> 与 `docs/sele-e.md` 的差异见 §13。本文是**实现依据**，与 `sele-e.md` 冲突处以本文为准。

---

## 1. 范围与前提

### 1.1 已定前提

| 编号 | 结论 |
|---|---|
| D1 | 双模式共存：有 `.sele/` 走新体系，只有 `.design/` 走旧体系；**不做迁移**，不提供 `import` |
| D2 | agent 可创建 git 提交，但只在 `sele/<line-id>` 分支；main 只由 merge 更新（v0.4 已实现） |
| D3 | `main` 是 git 分支，merge 是真实合并；v0.4 已实现 `line merge` |
| D4 | 验收由工具**记账**，不执行命令：`sele acceptance pass/fail --evidence` |
| D5 | 元数据用 **JSON**（复用 nlohmann/json），不引入 YAML |
| D6 | `.sele/` 分 tracked 与 runtime 两区：规格与里程碑入库，运行态本机忽略 |
| D7 | `line.json` 为权威状态、`log.jsonl` 为审计；`fsck` 对账，v0.3 不做事件重放重建 |
| D8 | v0.3 = 多线共存 + 单线单写者；v0.4 补 `line merge`/`line revert`，v0.5 补 `task revert`，v0.6 补 `line rebase`，v0.7 补 `worktree`，v0.9 补 fork/join，v0.10 补 archive；不做跨仓库 |

### 1.2 v0.4 仍不做

跨仓库线、Web UI、YAML 元数据、模块强制白名单、任务级验收映射、验收自动执行。

### 1.3 单仓库边界

`.sele/` 每仓库一份（与 `.git/` 相同），一条 line 只作用于当前仓库。跨仓库目标属 v0.4+。

---

## 2. 术语

| 术语 | 定义 | 与 git 的关系 |
|---|---|---|
| Line | 一个带验收标准的目标，可独立推进 | 对应分支 `sele/<line-id>` |
| Task | 线上的顺序工作单元 | 对应一次或多次 commit |
| Checkpoint | Task 执行过程中的提交快照 | 就是 git commit |
| Acceptance | 线级可判定验收项，由 agent/人记账 | 不对应 git 对象 |
| Session | 一个 agent/会话的当前 line 与 task 指针 | 对应 per-worktree HEAD |
| Journal | 里程碑日志 `log.jsonl` | 对应 reflog |

---

## 3. 目录布局

```
.sele/
├── .gitignore                 # 内容：runtime/
├── config.json                # tracked
├── log.jsonl                  # tracked，里程碑，append-only
├── lines/
│   └── <line-id>/
│       ├── line.json          # tracked，规格 + 线级状态
│       └── tasks/
│           └── T1.json        # tracked，任务规格
├── archive/                   # tracked，merged/abandoned 归档（line archive）
└── runtime/                   # ignored
    ├── state/<line-id>.json   # 任务状态、checkpoint、验收结果、version
    ├── sessions/<sid>.json    # 会话指针
    ├── events.jsonl           # 细粒度事件，append-only
    └── worktrees/             # 每个 line 的 worktree 指针（ignored）
```

### 3.1 为什么状态要拆成两份

`.sele/` 位于工作树内且部分被跟踪，如果把高频状态（task running / checkpoint / acceptance 结果）写进被跟踪文件，每次命令都会让工作树变 dirty，导致 `git switch`、`task done` 的干净性检查失效。

因此：

| 文件 | 内容 | 变更频率 | 入库 |
|---|---|---|---|
| `lines/<id>/line.json` | id/type/goal/acceptance/modules/created_at/frozen/status | 低（new/open/done/abandon） | 是 |
| `lines/<id>/tasks/T1.json` | id/title/depends/modules | 低（add） | 是 |
| `runtime/state/<id>.json` | task 状态、checkpoint sha、验收结果、version | 高（每次 start/done） | 否 |
| `runtime/sessions/<sid>.json` | 当前 line/task | 高 | 否 |
| `log.jsonl` | line 级里程碑 + task.done + acceptance 结果 | 低 | 是 |
| `runtime/events.jsonl` | task/acceptance 过程事件 | 高 | 否 |

线级状态（`line.json.status`）入库，因为它是团队要 review 的事实；`line done` 默认提交元数据（`--no-commit` 可关闭），`line open` 会把计划提交到 base（§6.2）。

### 3.2 克隆后的状态

克隆仓库后 `runtime/` 不存在。所有命令按需 `create_directories`，并用 `log.jsonl` 重建：line 定义、`task.done`（含 commit sha）、`acceptance.pass/fail` 都在 `log.jsonl` 里，因此 `sele fsck --fix` 可把 runtime/state 补齐到"已完成"以上的事实；只有 `running`/`failed`/`blocked` 这类过程态会丢失（回到 pending）。`overview` 会标注 `state: rebuilt`。

---

## 4. 数据模型

### 4.1 `config.json`

```json
{
  "schema_version": 1,
  "actor": "git",
  "modules": { "allow": [] },
  "checkpoint": { "strategy": "git-commit", "allow_empty": false }
}
```

| 字段 | 约束 |
|---|---|
| `schema_version` | 固定 1；读到其他值退 4 |
| `actor` | `"git"` 表示取 `git config user.name`；也可写死字符串；环境变量 `SELE_ACTOR` 优先级最高 |
| `modules.allow` | 空数组 = 不限制；非空时 `line check` 校验所有 modules 已登记 |
| `checkpoint.strategy` | v0.3 只认 `"git-commit"`，其他值退 4 |

### 4.2 `lines/<id>/line.json`

```json
{
  "schema_version": 1,
  "version": 1,
  "id": "20261029-fix-login",
  "type": "fix",
  "goal": "登录失败时给出可操作错误",
  "acceptance": [
    {"id": "A1", "desc": "错误密码返回 401", "check": "pytest tests/test_login.py::test_wrong_pwd"}
  ],
  "modules": ["auth", "ui"],
  "created_at": "2026-10-29T06:12:00Z",
  "created_by": "alice",
  "status": "active",
  "frozen": {
    "goal_sha256": "…",
    "acceptance_sha256": "…",
    "at": "2026-10-29T06:13:00Z"
  },
  "blocked": null,
  "closed": null
}
```

| 字段 | 约束 |
|---|---|
| `version` | 每次写入 +1，用于乐观并发（§8.2） |
| `id` | `^\d{8}-[a-z0-9][a-z0-9-]*$`，UTC 日期前缀，全局唯一 |
| `type` | `feature` / `fix` / `chore` / `refactor` / `spike` |
| `goal` | 非空，≤120 字 |
| `acceptance` | ≥1 条，每条 `id` 唯一（`A<n>`），`desc` 与 `check` 非空 |
| `modules` | ≥1 个，`^[a-z][a-z0-9-]*$` |
| `status` | `draft` / `active` / `blocked` / `done` / `abandoned` |
| `frozen` | `draft` 时为 `null`；`open` 后为哈希对象；`amend` 后重算 |
| `blocked` | 非 blocked 时为 `null`，否则 `{reason, at}` |
| `closed` | 非 done 时为 `null`，否则 `{at, acceptance_summary: {"pass": n, "fail": n}}` |

### 4.3 `lines/<id>/tasks/T<n>.json`

```json
{
  "schema_version": 1,
  "id": "T1",
  "title": "复现 401 分支",
  "depends": [],
  "modules": ["auth"],
  "created_at": "2026-10-29T06:14:00Z",
  "created_by": "agent"
}
```

- `id` 形如 `T1`，按创建顺序递增，由工具分配，不接受外部指定。
- `depends` 是**显式**的 task id 数组；`task add` 时工具把"上一个 task"写进 `depends`，落盘后不再隐式推导。
- v0.3 要求线性链（§5.3），因此 `depends` 长度恒为 0（根）或 1（唯一前驱）。

### 4.4 `runtime/state/<line-id>.json`

```json
{
  "schema_version": 1,
  "version": 7,
  "line": "20261029-fix-login",
  "tasks": {
    "T1": {
      "status": "done",
      "checkpoint": "3f2a9c1",
      "started_at": "2026-10-29T06:15:00Z",
      "finished_at": "2026-10-29T06:20:00Z",
      "reason": null
    }
  },
  "acceptance": {
    "A1": {"status": "pass", "evidence": "pytest 12 passed", "by": "agent", "at": "2026-10-29T06:21:00Z"}
  }
}
```

- `version` 每次写入 +1，用于乐观并发（§8.2）。
- 缺失的 task 视为 `pending`，缺失的 acceptance 视为 `pending`。
- `reason` 存 fail / block 的原因。

### 4.5 `runtime/sessions/<sid>.json`

```json
{
  "schema_version": 1,
  "session": "s-3f2a",
  "line": "20261029-fix-login",
  "task": "T1",
  "updated_at": "2026-10-29T06:20:00Z"
}
```

会话 id 来源优先级：`--session` 参数 > 环境变量 `SELE_SESSION` > `runtime/sessions/default.json`。`line switch` 与 `task start` 更新指针。

### 4.6 `log.jsonl`（tracked）

每行一个 JSON，append-only：

```json
{"ts":"2026-10-29T06:12:00Z","actor":"alice","op":"line.new","line":"20261029-fix-login"}
{"ts":"2026-10-29T06:13:00Z","actor":"alice","op":"line.open","line":"20261029-fix-login","frozen":"a1b2…"}
{"ts":"2026-10-29T06:30:00Z","actor":"agent","op":"line.done","line":"20261029-fix-login"}
```

`op` 枚举：`line.new` / `line.open` / `line.amend` / `line.reopen` / `line.block` / `line.unblock` / `line.done` / `line.abandon` / `task.done` / `acceptance.pass` / `acceptance.fail`。

`task.done` 行额外带 `task` 与 `commit`；`acceptance.*` 行额外带 `acceptance` 与 `evidence`。这三类写入 `log.jsonl` 的理由：它们是跨机器可重建的事实，克隆后 `fsck --fix` 靠它们恢复 runtime 状态。

### 4.7 `runtime/events.jsonl`（ignored）

```json
{"ts":"…","actor":"agent","op":"task.start","line":"…","task":"T1"}
{"ts":"…","actor":"agent","op":"task.checkpoint","line":"…","task":"T1","commit":"3f2a9c1"}
{"ts":"…","actor":"agent","op":"acceptance.pass","line":"…","acceptance":"A1","evidence":"pytest 12 passed"}
```

`op` 枚举：`task.add` / `task.start` / `task.checkpoint` / `task.fail` / `task.retry` / `task.block` / `task.unblock` / `line.switch`。

`task.done` 与 `acceptance.pass/fail` 同时写 `log.jsonl` 与 `events.jsonl`（前者供重建，后者供本机审计）；其余事件只写 `events.jsonl`。

### 4.8 JSON 序列化约定

- 2 空格缩进，键按固定顺序输出（实现里用有序构造，不依赖 `nlohmann::json` 默认的键排序语义）。
- 字段只增不改；新增字段不提升 `schema_version`，语义变更才提升。
- 写入一律原子（临时文件 + `rename`），复用 `core/fs`。

---

## 5. 状态机

### 5.1 Line

```
draft ──open──> active ──block──> blocked ──unblock──> active
  │               │  ↑                                  │
  │               │  └────────── reopen（done→active）───┘
  │               ├──done──> done ──merge──> merged
  └──abandon──> abandoned <──abandon── active / blocked
```

| 迁移 | 命令 | 前置条件 | 副作用 |
|---|---|---|---|
| draft → active | `line open` | ≥1 task、goal/acceptance/modules 非空、在 git 仓库内、工作树干净（§8.5）、分支可创建 | 计算并写入 `frozen`；`git switch -c sele/<id>`；写 `log.jsonl` |
| active → blocked | `line block --reason` | 有 reason | 写 `blocked`；写日志 |
| blocked → active | `line unblock` | — | 清 `blocked`；写日志 |
| active → done | `line done` | 全部 task `done`、全部 acceptance `pass` | 写 `closed`；写日志 |
| done → active | `line reopen --reason` | 有 reason | 清 `closed`；保留 `frozen`；写日志 |
| done → merged | `line merge --confirm` | done、工作树干净（§8.5）、`--confirm`、base 分支存在 | 标记 `merged`、记录 merge commit、切到 base 合并、写日志 |
| draft/active/blocked → abandoned | `line abandon --reason` | 有 reason | 写 `status=abandoned`；目录保留不移动；写日志 |
| active/draft/blocked 状态不变 | `line amend` | 有 reason | 重算 `frozen`；写日志 |

- `done` 状态不可 `amend`，必须 `reopen` 后改。
- `merged` 为终态：不可 `reopen`/`amend`/`abandon`/`switch`/`task add`。
- `line revert` 不改迁移表，而是把 line 恢复到某个 task checkpoint 时的状态（通常为 `active`）；仅未合并/未放弃的线可用。
- task 的 blocked **不改变** line 状态；`line status` 显示阻塞点，`line done` 因 task 未 done 而拒绝。

### 5.2 Task

```
pending ──start──> running ──done──> done
                     │  ↑
                     │  └── retry（failed→running）
                     ├──fail──> failed
                     └──block──> blocked ──unblock──> running
```

| 迁移 | 命令 | 前置 | 副作用 |
|---|---|---|---|
| pending → running | `task start` | line 为 active | 写 runtime；写 events |
| running → done | `task done` | 存在 checkpoint、工作树干净（§8.5）、line 为 active | 写 runtime；写 `log.jsonl`（含 commit）+ events |
| running → failed | `task fail --reason` | 有 reason | 写 runtime；写 events |
| failed → running | `task retry` | line 为 active | 清 reason；写 events |
| running → blocked | `task block --reason` | 有 reason | 写 runtime；写 events |
| blocked → running | `task unblock` | line 为 active | 清 reason；写 events |

- 目标状态已达成时命令幂等：退出 0，`changed:false`，不写文件。
- 其他非法迁移退出 2，错误带 `next` 提示合法命令。
- 同一 line 同时最多一个 task 为 `running`（单写者），第二个 `task start` 退 2。

### 5.3 任务图约束（v0.9）

- 任务图是单根、无环的有向图；每个 task 的 `depends` 必须指向同一 line 内已存在的 task。
- 线性推进时每个 task 恰好一个前驱；`task add` 默认把链尾写为 `depends`。
- 并行组：`task add --group <g>` 的成员必须共享同一组 `depends`（fork 点）；组内 modules 重叠只告警（worktree 隔离，合并期才可能冲突）。
- join 点：一个 task 可不带 `--group`，用多个 `--depends` 指向组内全部成员；`task start` 要求所有前驱 `done`。
- 组内可同时多个 `running`；组外不得与组内同时运行（`task start` 拒绝）。
- `line check` 的 `task_graph` 校验：前驱存在、单根、无环、全部可达、组内 depends 一致；`parallel_groups` 报告模块重叠警告。
- 违反 `task_graph` → `line check` 退出 3，`line open` 拒绝。

---

## 6. 命令规格

### 6.1 命令总览

```
sele init [--legacy]                        创建 .sele/（默认）或 .design/+.agent/（--legacy）
sele line new <slug> …                      创建 line（draft）
sele line list [--status <s>] [--all]
sele line show <id>
sele line check <id>
sele line open <id>
sele line switch <id>
sele line block <id> --reason <s>
sele line unblock <id>
sele line amend <id> --reason <s> [--goal <s>] [--acceptance …] [--modules a,b]
sele line done <id> [--no-commit]
sele line merge <id> [--confirm] [--onto <branch>] [--delete-branch]
sele line revert <id> --to <task>
sele line rebase <id> [--onto <branch>] [--continue] [--abort]
sele line archive <id>
sele line reopen <id> --reason <s>
sele line abandon <id> --reason <s>

sele task add "<title>" --modules a,b [--line <id>] [--group <g>] [--depends <task>]...
sele task list [--line <id>]
sele task start <task> [--line <id>]
sele task checkpoint <task> [--line <id>] [--msg <s>]
sele task done <task> [--line <id>]
sele task fail <task> [--line <id>] --reason <s>
sele task retry <task> [--line <id>]
sele task block <task> [--line <id>] --reason <s>
sele task unblock <task> [--line <id>]
sele task revert <task> [--line <id>]

sele acceptance pass|fail <acceptance> [--line <id>] --evidence <s> [--by <s>]

sele overview
sele log [--line <id>] [--limit <n>] [--events]
sele fsck [--fix]
sele worktree add <line> [--path <dir>]
sele worktree list
sele worktree remove <line> [--force]
sele version
```

- 所有命令支持 `--output text|json`（默认 text）与 `--root <path>`（默认当前目录，向上查找 `.sele/`）。
- 全局 `--session <id>` 选择会话指针（默认 `$SELE_SESSION`，再退化为 `default`）。
- `<line>` 通过 `--line` 传入；省略时取会话指针；无会话指针退 2，`next` 给出 `sele line list`。
- `<task>`、`<acceptance>` 参数是 task/acceptance 的 `id`（`T1` / `A1`）。

### 6.2 关键命令细节

#### `sele init [--legacy]`

- 默认创建 `.sele/`：目录、`config.json`、`log.jsonl`（空文件）、`.gitignore`（内容 `runtime/`）、`runtime/` 子目录；若目标目录没有 `AGENTS.md`，一并写入内置的 agent 入口模板（已有则**不覆盖**）。
- 目标已存在 `.sele/` → 退 2。已有 `.design/` 或 `.agent/` 时**允许**创建 `.sele/`，旧目录保留不动，输出 `legacy_present:true`；两者共存时旧命令仍可用（§10.1）。
- `--legacy`：保持 v0.2 行为，创建 `.design/` + `.agent/` 与模板。
- 不自动 `git init`；非 git 仓库也可 init，但 `line open` 与 `task checkpoint` 需要 git。

#### `sele line new <slug> …`

```
sele line new fix-login \
  --goal "登录失败时给出可操作错误" \
  --acceptance "错误密码返回 401::pytest tests/test_login.py::test_wrong_pwd" \
  --acceptance "成功登录返回 token::pytest tests/test_login.py::test_success" \
  --modules auth,ui \
  --type fix \
  [--id 20261029-fix-login]
```

- 位置参数是 slug（`^[a-z0-9][a-z0-9-]*$`）；id 默认 `<UTC 日期>-<slug>`，`--id` 可覆盖但必须符合完整格式。
- acceptance 可重复；`<desc>::<check>` 用第一个 `::` 分割，两部分都非空。
- 工具自动分配 `A1..An`。
- 同一仓库内 id 已存在 → 退 2。
- 创建后状态 `draft`，不建 git 分支；分支在 `open` 时创建。会话指针指向新 line。

#### `sele line check <id>`

检查项：`linear_chain`（单根、单前驱、无环）、`frozen_acceptance`（active 后 goal/acceptance 哈希未变）、`module_registry`（白名单非空时校验）、`done_consistency`、`cross_line_modules`（跨 line 同模块：默认只告警，`--strict` 时计为失败）。全部通过退 0，否则退 3。

#### `sele line open <id>`
前置：`draft`、≥1 task、字段完整、git 仓库且有提交、工作树干净（§8.5）、`sele/<id>` 分支不存在或可切换。
副作用：若当前在 base 分支，先把 `.sele` 计划提交（`chore(sele): plan <id>`），让 base 也拥有 `.sele`；记录 `base_commit = HEAD`；写 `frozen`（对归一化后的 goal 与 acceptance 取 SHA-256）、`status=active`、`git switch -c sele/<id>`、写 `log.jsonl`，并把会话指针指向该 line。
失败：工作树 dirty → 退 2，`next: ["git status", "sele task list"]`。

#### `sele line switch <id>`

前置：line 存在且状态为 `draft`/`active`/`blocked`；工作树干净（§8.5）。
副作用：`git switch sele/<id>`（分支不存在则 `-c`）、写会话指针、写 `runtime/events.jsonl`。

#### `sele line amend <id> --reason <s>`

- 只允许 `draft` / `active` / `blocked`。
- 可改 `goal` / `acceptance` / `modules`；至少给一项，且必须有 reason。
- `--acceptance` 提供时**整体替换**并重新分配 `A1..An`，旧验收结果按 id 对不上则丢弃（已写入 `log.jsonl` 的历史保留）。
- 重算并覆盖 `frozen`，旧哈希写入 `events.jsonl`（`op=line.amend`，含 `old_frozen`）。
- `active` 后 `acceptance` 的 `id` 不可删除；新增验收项需要 reason。

#### `sele line done <id> [--no-commit]`

- 前置：`active`、全部 task `done`、全部 acceptance `pass`。
- 副作用：`status=done`、`closed={at, acceptance_summary}`、写 `log.jsonl`。
- 默认在 line 分支上提交 `.sele` 元数据（`chore(sele): line <id> done`）；`--no-commit` 只写文件。

#### `sele line merge <id> [--confirm] [--onto <branch>] [--delete-branch]`

- 前置：`done`、`--confirm`、git 仓库有提交、工作树干净（§8.5）、`sele/<id>` 分支存在、base 分支存在（默认 `main`）。
- 命令会自行切到 line 分支再加载状态（base 上的 `line.json` 可能还是 draft 计划）。
- 在 line 分支上先把 `status` 置为 `merged` 并提交（`chore(sele): mark <id> merged`），保证状态随合并进入 base。
- 切到 base，执行 `git merge --no-ff -m "sele: merge <id>" sele/<id>`。
- 冲突：立即 `git merge --abort`，切回 line 分支，把状态回滚为 `done` 并提交（`unmark`），退 2 并列出冲突文件；人工在 line 分支解决后重试。
- 成功：在 base 上记录 `merged.{at,commit}` 并提交（`record merge`），写 `log.jsonl` 的 `line.merge`；`--delete-branch` 时删除 `sele/<id>`。

#### `sele line revert <id> --to <task>`

- 前置：git 仓库、工作树干净（§8.5）、line 不是 `merged`/`abandoned`、`<task>` 存在且有 checkpoint。
- `git reset --hard <task.checkpoint>`（只动 line 分支；未合并前的私有历史）。
- 重置状态：该 task 保留 `done`；其后的 task 回到 `pending`、清空 checkpoint/reason/finished_at；所有 acceptance 回到 `pending`；line 回到 checkpoint 时的 `active`（`closed` 随之消失）。
- runtime 中不再属于 task 列表的条目被删除；写 `log.jsonl` 的 `line.revert`（含 `to` 与 `commit`）。
- 不自动提交：revert 后的元数据靠后续 checkpoint 或手动提交。

#### `sele line rebase <id> [--onto <branch>] [--continue] [--abort]`

- 前置（普通 rebase）：git 仓库、工作树干净（§8.5）、line 非 merged/abandoned、base 分支存在、没有正在进行的 rebase。
- `git rebase <onto>`（默认 `main`）；成功后更新 `base_commit` 为 base 的 HEAD，提交元数据（`chore(sele): rebase <id>`），写 `line.rebase` 日志。
- 冲突：不自动 abort，rebase 停在冲突状态，退 2 并列出冲突文件；`onto` 记入 `runtime/state/<id>.json` 的 `rebase_onto`，供 `--continue` 使用。
- `--continue`：`git rebase --continue`（禁用编辑器）后更新 `base_commit`、清空 `rebase_onto`、提交元数据。
- `--abort`：`git rebase --abort`，清空 `rebase_onto`，不改元数据。
- `--continue` / `--abort` 互斥，且要求 rebase 正在进行。

#### `sele line archive <id>`

- 前置：line 为 `merged` 或 `abandoned`，且没有 worktree 指针（有则先 `worktree remove`）。
- 把 `.sele/lines/<id>/` 移到 `.sele/archive/<id>/`；`line list` 默认不再显示，`--all` 会列出并标记 `archived: true`。
- 归档后该 line 只读：`load` 仍可 `show`/`check`，所有写命令退 2（`save_*` 统一拒绝）。
- 若会话指针指向该 line，清空指针；写 `line.archive` 日志；runtime state 保留供 `show` 使用。
- 重复归档幂等：`changed:false`。

#### `sele task add "<title>" --modules a,b [--line <id>] [--group <g>] [--depends <task>]...`

- 自动分配 `T<n>`；`depends` 默认链尾；组内新成员默认继承组内已有的 `depends`。
- `--depends` 可重复，用于 join 点（多个前驱）；每个前驱必须已存在，否则退 1。
- `--group` 的 id 为 `[A-Za-z][A-Za-z0-9-]*`；组内成员必须共享同一组 `depends`，否则 `line check` 退 3。
- 只允许 `draft` / `active` / `blocked` 的 line；`done` / `abandoned` / `merged` 退 2。

#### `sele task start <task> [--line <id>]`

- 前置：line `active`、task `pending`、该 line 无其他 `running` task。
- 副作用：runtime 写 `running`、会话 `task` 指针更新、写 events。

#### `sele task checkpoint <task> [--line <id>] [--msg <s>]`

- 前置：task `running`、line `active`、当前分支为 `sele/<line>`。
- 行为：`git add -A && git commit -m <msg|默认>`；无改动时不提交，复用上一个 checkpoint，`changed:false`。`git add -A` 会把 `.sele` 下尚未提交的元数据（`line.json`、`tasks/*.json`、`log.jsonl`）一并提交，因此元数据随 checkpoint 入库；`runtime/` 被 `.sele/.gitignore` 排除。
- 副作用：runtime 写 `checkpoint=<sha>`、写 events。
- 默认提交信息：`<line-id>: <task-id> <title>`。
- 这是 v0.3 中**唯一由 sele 主动创建 commit** 的命令。

#### `sele task done <task> [--line <id>]`

- 前置：`running`、存在 checkpoint、工作树干净（§8.5）、line `active`。
- 失败：dirty → 退 2，`next: ["sele task checkpoint <line> <task>"]`。
- 副作用：runtime `done` + `finished_at`；写 `log.jsonl`（含 `commit`）；写 events。

#### `sele task revert <task> [--line <id>]`

- 前置：git 仓库、工作树干净（§8.5）、line 不是 `merged`/`abandoned`、task 存在。
- 非根 task：`git reset --hard <前驱.checkpoint>`；前驱必须有 checkpoint，否则退 2。
- 根 task：`git reset --hard <line.base_commit>`，line 回到 `draft`（`frozen` 清空），需 `line open` 重新开始。
- 重置集合：该 task 及其之后所有 task → `pending`，清空 checkpoint/reason/finished_at/started_at；acceptance 全部 `pending`；非根时 line 若为 `done` 则回到 `active`。
- 与 `line revert --to <task>` 的区别：`line revert` 保留目标 task 的完成状态；`task revert` 撤销目标 task 本身。

#### `sele worktree add|list|remove`

- `add <line> [--path <dir>]`：要求 `sele/<line>` 分支存在且未被当前工作树检出；`git worktree add <dir> sele/<line>`；目标默认 `<repo 父目录>/<repo>-wt-<line>`；指针写到 `.sele/runtime/worktrees/<line>.json`（ignored）。
- `list`：读取指针，`present=false` 表示目录已不存在；JSON `items[{line,branch,path,present}]`。
- `remove <line> [--force]`：`git worktree remove <path>`，删除指针；有未提交改动时需 `--force`。
- worktree 内的 `.sele/runtime/` 是本机新建的，会话指针与 task 运行态按工作树独立；`.sele/lines/` 随分支内容。

#### `sele acceptance pass|fail <acceptance> [--line <id>] --evidence <s>`

- 前置：line 为 `active`（`done` 后不再接受记账，避免推翻已关闭结论）；acceptance id 存在。
- 副作用：runtime 写结果；写 `log.jsonl`（含 evidence）+ events。`--by` 默认取 actor。

#### `sele overview`

输出全局概览：每条 line 的 id/status/task 进度/验收通过数/阻塞原因；当前会话指针；`.sele` 元数据是否有未提交改动（`git status --porcelain .sele` 非空时提示）。

#### `sele log [--line <id>] [--limit <n>] [--events]`

读 `log.jsonl`（里程碑），默认最近 20 条；`--output json` 输出 `items` 数组。`--events` 时合并 `runtime/events.jsonl`（`task.start`/`checkpoint`/`fail` 等过程事件），按 `ts` 升序排序后统一过滤与截断，每条事件带 `source: "log"|"events"`；两个文件的坏行数合并为 `bad_lines`。

#### `sele fsck [--fix]`

| 检查 | 严重度 | `--fix` |
|---|---|---|
| `schema_version` 合法、JSON 可解析 | 失败(3) | 不修 |
| line/task 字段齐全、id 格式、枚举合法 | 失败(3) | 不修 |
| 线性链：单根、单前驱、无环、无孤儿 | 失败(3) | 不修 |
| `frozen` 与当前 goal/acceptance 哈希一致（active 后） | 失败(3) | 不修 |
| `line.json.status=done` 但存在未 done task 或未 pass 验收 | 失败(3) | 不修 |
| `runtime/state` 中 task 为 done 但无 checkpoint | 警告 | 不修 |
| 会话指针指向不存在/已闭合的 line | 警告 | 重建或清除会话 |
| `log.jsonl` 存在损坏行 | 失败(3) | 报告行号，不修 |
| `.sele` 跟踪文件有未提交改动 | 提示 | 不修 |
| runtime/state 缺失但 log 有 `task.done` / `acceptance.*` | 提示 | 从 `log.jsonl` 重放，补齐 done/结果与 checkpoint sha |

退出码：0 无失败，3 有失败，4 环境错误。

---

## 7. 输出契约

### 7.1 text

- `line list`：每行 `<status>  <id>  <done>/<total> tasks  <pass>/<total> acceptance`。
- `line show`：头部四行（Line/Goal/Modules/Status），随后 task 列表与验收列表，阻塞原因与 `next` 建议。
- `line check`：首行 `Check passed` / `Check failed`，逐项 `[ok]` / `[fail]` / `[warn]`。
- `task list`：`T1 [done] 复现 401 分支 (modules: auth)`。
- `overview`：每条 line 一行摘要 + 会话行 + 未提交提示。
- `log`：`<ts>  <actor>  <op>  <line>[/<task>]`。
- `fsck`：首行 `Fsck passed` / `Fsck failed`，逐项结果。

### 7.2 json

所有命令 `--output json` 输出单个对象（列表类命令输出数组或 `{items:[…]}`），通用字段：

| 字段 | 说明 |
|---|---|
| `command` | 如 `line.open`、`task.done` |
| `changed` | 是否真的写盘 |
| `line` / `task` / `acceptance` | 相关 id（适用时） |
| `path` | 受影响文件（写命令） |

错误统一：

```json
{"error":"task T2 is pending, not running","suggestion":"Run sele task start","next":["sele task start 20261029-fix-login T2"]}
```

`next` 只在能给出确定后续动作时出现。

### 7.3 退出码

沿用 0/1/2/3/4：0 成功；1 用法错误；2 状态冲突或非法迁移；3 `line check` / `fsck` 未通过；4 环境错误（无 `.sele`、无 git、IO 失败、schema 不支持）。

---

## 8. 并发与一致性

### 8.1 单写者假设

v0.3 假定同一 line 同时只有一个写入者（一个 agent 会话）。跨 line 可并行，但每个 line 仍是单写者。多 agent 抢同一 line 不在 v0.3 保证范围内，由 `version` 检测（§8.2）。

### 8.2 乐观并发

- `runtime/state/<id>.json` 与 `line.json` 都含 `version`（line.json 加 `version` 字段，初始 1）。
- 写流程：读文件（记 version）→ 计算新内容 → 重新读取比对 version → 一致则原子写（version+1）→ 不一致退 2，`next: ["sele line show <id>"]`。
- 不引入锁文件；崩溃不会留下孤儿锁。

### 8.3 原子写

统一 `core/fs::write_atomic`：写 `<path>.tmp` → `flush` → 校验流状态 → `rename`。失败抛异常，命令层映射为 4。

### 8.4 工作树与分支

- line ↔ 分支 `sele/<line-id>` 1:1。
- 每个 line 可额外检出到 `sele worktree add` 创建的独立工作树；指针在 `.sele/runtime/worktrees/<id>.json`，不计入主工作树的状态。
- v0.3 不创建 worktree；`line switch` 用 `git switch`。
- `line open` 在切到 line 分支前把 `.sele` 计划提交到 base，保证 base 也有 `.sele`；`line done` 默认提交元数据；`line merge` 依次在 line 分支标记、在 base 合并、在 base 记录 merge commit。
- 所有 git 调用使用参数数组（`fork`/`execvp` 或 `posix_spawn`），**不用 `popen` 拼接命令**（旧 `core/git` 保留给 legacy 命令，新代码用 `core/gitrt`）。

### 8.5 干净性判定

干净性判定只看**代码**，不看 `.sele/`：

```
git status --porcelain --untracked-files=no -- . ':(exclude).sele'
```

为空即视为干净。`line open` / `line switch` / `task done` 依赖此判定。

排除 `.sele/` 是必需的：`line new` / `log` 追加 / 状态写入都会改动被跟踪的元数据，若把它们算进 dirty，`line open` 会在自己刚写完文件后失败；而 `.sele` 的未提交改动由一次 `task checkpoint` 的 `git add -A` 顺带提交，`line done` 默认提交，`--no-commit` 可关闭，`overview` 会提示还有未提交的元数据。

`.sele/runtime/` 由 `.sele/.gitignore` 排除在版本控制之外，但同样不计入干净性判定（它在排除路径内）。

---

## 9. 审计

| 文件 | 入库 | 内容 | 用途 |
|---|---|---|---|
| `log.jsonl` | 是 | line 级里程碑 + `task.done` + acceptance 结果 | 跨机器可追溯与重建；可用 `merge=union` 合并 |
| `runtime/events.jsonl` | 否 | 过程事件（start/checkpoint/fail/block） | 本机审计 |

`.sele/.gitattributes`（init 时创建）：

```
log.jsonl merge=union
```

事件丢失的容忍度：`runtime/events.jsonl` 丢失不影响正确性，只影响本机过程审计；`log.jsonl` 损坏由 `fsck` 报错。

---

## 10. 双模式共存

### 10.1 检测顺序

1. 从 `--root`（或当前目录）向上查找 `.sele/` → 新模式。
2. 未找到，向上查找 `.design/` 与 `.agent/` → 旧模式（输出一条提示：`legacy project (.design/.agent); see sele --help`）。
3. 都没有 → 报错退 4，`next: ["sele init"]`。

迁移期例外：同一目录同时存在 `.sele/` 与 `.design/` 时，旧命令仍可用（不会强制拒绝），新命令照常使用。只有"只有 `.sele/`"时才拒绝旧命令。

### 10.2 命令归属

| 项目类型 | 可用 | 不可用（退 4） |
|---|---|---|
| 新模式（`.sele/`） | `init`（默认）、`line`、`task`、`acceptance`、`overview`、`log`、`fsck`、`version` | `new`、`append-decision`、`status`、`lint`、`close` |
| 旧模式（`.design/`） | `init --legacy`、`new`、`append-decision`、`status`、`lint`、`close`、`version` | `line`、`task`、`acceptance`、`overview`、`log`、`fsck` |

不可用命令的错误信息必须给出原因与替代：

```
$ sele close 20261029-fix-login      # 在 .sele 项目里
Error: close is a legacy command for .design/.agent projects
Suggestion: Use sele line done <id> to finish a line
next: ["sele line list", "sele line show 20261029-fix-login"]
```

### 10.3 `version`

两模式通用，输出 `sele 0.3.0` 及依赖版本。

---

## 11. 测试计划

单二进制，Linux 上 `tests/sele_tests.sh` 当前实现 59 条：下表计划集已全部覆盖，另增 `task fail/retry`、`task block/unblock`、`line block/unblock`、`line abandon`、dirty 工作树下 `open/switch` 的拒绝、done line 拒绝新任务、`check --strict` 跨线冲突、`line merge` 成功/冲突回滚/删除分支、`line revert` 回退与拒绝已合并线、`task revert` 中间/根任务与前置检查、`line rebase` 成功/冲突 abort/冲突 continue、`worktree` 增删查与前置拒绝、`log --events` 合并、fork/join 并行与组约束、`archive` 移动/只读/幂等/前置拒绝、`init` 生成 AGENTS.md 与不覆盖已有文件、与 `.design/.agent` 共存初始化。Windows 的 ps1 新体系脚本暂未提供（旧 cli 测试仍双平台）。

| # | 用例 | 断言 |
|---|---|---|
| 1 | `init` 建 `.sele/` | 目录、config、log、gitignore 存在；重复 init 退 2 |
| 2 | `init --legacy` | 建 `.design/`+`.agent/`，行为同 v0.2 |
| 3 | `line new` 最小参数 | id 为 `<日期>-<slug>`，status=draft，A1 自动分配 |
| 4 | `line new` 重复 id | 退 2 |
| 5 | `line new` 非法 slug / 空 goal / acceptance 无 `::` | 退 1 |
| 6 | `line open` 成功 | frozen 写入、status=active、分支存在、log 追加 |
| 7 | `line open` 无 task | 退 2 |
| 8 | `line open` dirty 工作树 | 退 2，next 含 git status |
| 9 | `task add` 自动 depends | 第二个 task 的 depends=[T1] |
| 10 | `task start` 成功后再次 start | 退 0，changed=false（幂等） |
| 11 | 第二个 running | 退 2 |
| 12 | `task done` 无 checkpoint | 退 2 |
| 13 | `task checkpoint` 后再 done | done、runtime 含 sha、events 两条 |
| 14 | `task fail` + `task retry` | failed→running，reason 清空 |
| 15 | `task block` + `task unblock` | blocked→running，line 状态不变 |
| 16 | 非法迁移（pending→done） | 退 2，next 给出 start |
| 17 | `acceptance pass` 记账 | runtime 与 events 一致 |
| 18 | `line done` 有 pending task / pending 验收 | 退 2 |
| 19 | `line done` 全部满足 | status=done，closed 写入，log 追加 |
| 20 | `line reopen` 后可 add task | status=active，done task 保留 |
| 21 | `line amend` 重算 frozen | 哈希变化，events 含 old_frozen |
| 22 | `line check` 线性链破坏 | 退 3，指出具体 task |
| 23 | `fsck` 正常 / 损坏 log 行 | 0 / 3，报告行号 |
| 24 | 双模式分派 | `.sele` 项目里 `sele close` 退 4 且提示；`.design` 项目里 `sele line` 退 4 |

另需回归：`version`、`--help` 每子命令含示例、所有 `--output json` 可被 `jq` 解析。

---

## 12. 实现落点

```
src/
├── main.cpp                 注册新命令；--session 全局选项
├── cli/
│   ├── init.cpp             双行为（默认 .sele，--legacy 旧）
│   ├── nextgen.hpp/.cpp     共享上下文、CliError、加载/保存、日志、链校验
│   ├── line.cpp             line 子命令组（含 check）
│   ├── task.cpp             task 子命令组
│   ├── acceptance.cpp       acceptance 子命令组
│   ├── overview.cpp         overview / log / fsck
│   └── （legacy 文件保持不动，仅在回调开头加 legacy_guard）
├── core/
│   ├── sele.hpp/.cpp        根发现、config、schema、utc、id 校验、JSON 读写
│   ├── model.hpp/.cpp       line/task/runtime 结构与序列化、验收摘要
│   ├── hash.hpp/.cpp        SHA-256（冻结 goal/acceptance）
│   ├── journal.hpp/.cpp     log/events 追加与坏行容忍
│   ├── session.hpp/.cpp     会话指针
│   ├── gitrt.hpp/.cpp       argv 形式 git 调用（status/switch/commit）
│   └── fs.hpp/.cpp          原子写、追加行
└── util/                    output 增加 next，exit_codes 不变
```

- 依赖仍只有 CLI11 2.4.2 + nlohmann/json 3.11.2；不引入 YAML。
- `CMakeLists.txt` 手工维护源文件列表（无 glob）。
- 版本升至 0.3.0。

---

## 13. 与 `docs/sele-e.md` 的差异

| 项 | sele-e.md | 本规格 | 理由 |
|---|---|---|---|
| 元数据格式 | YAML | JSON | 不新增依赖；YAML 隐式类型有坑 |
| 状态存储 | 单一 line/task 文件含 status | 规格入库 + 状态入 runtime | 避免每次命令弄脏工作树 |
| worktree | 内嵌 `.sele/lines/<id>/worktree/` | v0.3 不做 | 内嵌污染 `git status`；单写者不需要 |
| fork/join | v0.1 实现 | 不做（数据结构预留） | 先验证状态机与审计 |
| merge/rebase/revert | v0.1 实现 | 不做，v0.4 | 复杂度最高、文档最薄 |
| acceptance 执行 | `line done` 时跑 check | 只记账 | 避免任意命令执行器 |
| line id | 人给 kebab-case | `<UTC 日期>-<slug>` | 防多 agent 撞名 |
| depends | 空=依赖前一个（隐式） | add 时落盘显式 id | 避免重排改变语义 |
| `.sele/main/` | 存在 | 删除 | main 是 git 分支 |
| 全局 HEAD | 隐含单指针 | 每会话指针 | 多 agent 并行 |
| fsck | 无 | 必需 | `.git` 类比的配套 |
| 验收冻结 | 无 | `open` 时冻结哈希 | 防止事后放宽验收 |
| task 的 line 选择 | 位置参数 `[<line>]` | `--line <id>`（省略取会话） | 避免与必填 `<title>/<task>` 位置参数歧义 |
| 双模式并存 | 未定义 | 同时存在时旧命令仍可用 | 迁移期安全，不阻断现有工作流 |

---

## 14. 开放问题（不影响 v0.3 开工）

1. `line done` / `line merge` 产生的元数据提交是否需要推送到远端？v0.3/v0.4 不推送。
2. `log` 是否应合并 `events.jsonl`（`--events`）？v0.3 不做。
3. 跨线 modules 冲突在 v0.3 只告警；v0.4 merge 时是否阻断，待定。
4. `main` 的基线记录：`base_commit` 已在 `line open` 时写入；`head_commit` 仍需时再补。
5. 多仓库线：v0.4+ 需要全局 `~/.sele/` 索引，本文不涉及。
