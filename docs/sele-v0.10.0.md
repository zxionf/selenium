# sele 0.10.0 使用手册（面向 agent）

> 适用版本：`sele 0.10.0`（`sele version` 可查）。  
> `.sele/` 是**工具私有目录**（类比 `.git/`），只能通过 `sele` 命令读写；不要用编辑器直接修改。  
> 旧的 `.design/` + `.agent/` 体系仍然存在，见 §12；新项目请用 `.sele/`。  

---

## 1. 核心模型

| 概念 | 含义 | 类比 git |
|---|---|---|
| Line | 一个带验收标准的目标 | 分支 `sele/<id>` |
| Task | 线上的工作单元（可并行分组） | commit 的产出 |
| Checkpoint | Task 执行中的提交快照 | commit |
| Acceptance | 线级验收项，只记账不执行 | — |
| Session | 当前会话指向的 line/task | per-worktree HEAD |

一条 line 的状态：`draft → active → done → merged`，另有 `blocked`、`abandoned`。
一个 task 的状态：`pending → running → done`，另有 `failed`、`blocked`。

**硬规则**：

- 结构（状态、勾选、指针）只由命令写；散文（goal、acceptance 描述、task 标题）自由写。
- `line done` 之前必须所有 task `done` 且所有 acceptance `pass`。
- `task done` 之前必须有 checkpoint，且工作树干净（忽略 `.sele/`）。
- 写命令幂等：目标状态已达成时退出 0、`changed:false`。

---

## 2. 前置条件

```bash
git init -b main
git add -A && git commit -m "init"      # 必须有初始提交
sele init                                # 创建 .sele/
```

- `line open` / `task checkpoint` / `line merge` / `line rebase` 要求 git 仓库且有提交。
- 工作树干净指：`git status --porcelain --untracked-files=no -- . ':(exclude).sele'` 为空。
- 任意命令可用 `--root <dir>` 指定项目根（默认当前目录，向上查找 `.sele/`）。
- 任意命令可用 `--output json` 获取机器可读结果（推荐 agent 始终使用）。
- 多 agent 并行时用 `--session <id>`（或环境变量 `SELE_SESSION`）隔离当前线指针。

---

## 3. 冷启动

```bash
sele overview --output json        # 全部 line、当前会话、元数据是否未提交
sele line show <id> --output json  # 目标 line 的任务、验收、阻塞点
sele task list --line <id> --output json
```

`overview` 的 `items[]` 与 `current` 可直接决定下一步。

---

## 4. 标准工作流

```bash
# 1) 建线（draft）
sele line new fix-login \
  --goal "登录失败时给出可操作错误" \
  --acceptance "错误密码返回 401::pytest tests/test_login.py::test_wrong_pwd" \
  --modules auth,ui --type fix

# 2) 加任务（线性链；自动 depends = 链尾）
sele task add "复现 401 分支" --modules auth
sele task add "实现错误提示" --modules ui

# 3) 开线：冻结验收、创建 sele/<id> 分支、提交 .sele 计划
sele line open <id>

# 4) 依次推进任务
sele task start T1
# ... 改代码 ...
sele task checkpoint T1 --msg "wip: repro"   # 唯一会自动 git commit 的命令
sele task done T1

sele task start T2
# ...
sele task checkpoint T2 --msg "feat: message"
sele task done T2

# 5) 记账验收（工具不执行命令）
sele acceptance pass A1 --evidence "pytest 12 passed"

# 6) 收线（默认提交 .sele 元数据）
sele line done <id>

# 7) 合并进 main
sele line merge <id> --confirm

# 8) 归档
sele line archive <id>
```

---

## 5. 命令表

### line

| 命令 | 关键参数 | 作用 |
|---|---|---|
| `line new <slug>` | `--goal`（必填）、`--acceptance "desc::check"`（可重复）、`--modules a,b`（必填）、`--type`、`--id` | 建 draft；id 默认 `<UTC日期>-<slug>` |
| `line list` | `--status`、`--all` | 列表；`--all` 含归档线（`archived:true`） |
| `line show [<id>]` | — | 任务、验收、阻塞点 |
| `line check [<id>]` | `--strict` | 任务图/冻结/模块检查；失败退 3 |
| `line open <id>` | — | draft→active；需 ≥1 task、git 干净 |
| `line switch <id>` | — | 切当前会话（git switch + 会话指针） |
| `line block <id> --reason` | 必填 | active→blocked |
| `line unblock <id>` | — | blocked→active |
| `line amend <id> --reason` | `--goal`/`--acceptance`/`--modules` | 改计划并重算冻结哈希 |
| `line done [<id>]` | `--no-commit` | active→done；默认提交元数据 |
| `line reopen <id> --reason` | 必填 | done→active |
| `line abandon <id> --reason` | 必填 | 放弃（保留记录） |
| `line merge <id> --confirm` | `--onto main`、`--delete-branch` | done→merged；冲突自动 abort 并退回 done |
| `line revert --to <task>` | 必填 | 回到某 task checkpoint（保留该 task done） |
| `line rebase [<id>]` | `--onto main`、`--continue`、`--abort` | 变基；冲突需人工解决后 continue |
| `line archive <id>` | — | merged/abandoned 移到 `.sele/archive/`，之后只读 |

### task

| 命令 | 关键参数 | 作用 |
|---|---|---|
| `task add "<title>"` | `--modules a,b`（必填）、`--line`、`--group <g>`、`--depends <t>`（可重复） | 追加任务；默认 depends=链尾 |
| `task list` | `--line` | 任务与状态 |
| `task start <task>` | `--line` | pending→running；前驱必须 done |
| `task checkpoint <task>` | `--msg`、`--line` | 提交代码+元数据，记录 checkpoint |
| `task done <task>` | `--line` | running→done；需 checkpoint 且工作树干净 |
| `task fail <task> --reason` | 必填 | running→failed |
| `task retry <task>` | — | failed→running |
| `task block <task> --reason` | 必填 | running→blocked |
| `task unblock <task>` | — | blocked→running |
| `task revert <task>` | `--line` | 撤销该 task 及其后续（根 task 则回到 draft） |

### acceptance / 全局

| 命令 | 关键参数 | 作用 |
|---|---|---|
| `acceptance pass\|fail <A-id>` | `--evidence`（必填）、`--by` | 记账验收结果 |
| `overview` | — | 全局概览 + 会话 + 未提交元数据 |
| `log` | `--line`、`--limit`、`--events` | 里程碑；`--events` 合并过程事件 |
| `fsck` | `--fix` | 校验 `.sele` 一致性；`--fix` 从 log 重建 runtime |
| `worktree add <line>` | `--path` | 为线创建独立工作树 |
| `worktree list` | — | 已记录的 worktree（`present` 表示目录存在） |
| `worktree remove <line>` | `--force` | 删除工作树并清指针 |
| `init` | `--legacy` | 建 `.sele/`；`--legacy` 建旧体系 |
| `version` | — | 版本与依赖版本 |

`<line>` 省略时取会话指针；`<task>` / `<A-id>` 是 `T1` / `A1` 形式的 id。

---

## 6. 状态与前置

### line 迁移

| 迁移 | 命令 | 前置 |
|---|---|---|
| draft→active | `line open` | ≥1 task、字段齐全、git 干净、有提交 |
| active→blocked | `line block` | reason |
| blocked→active | `line unblock` | — |
| active→done | `line done` | 全部 task done、全部 acceptance pass |
| done→active | `line reopen` | reason |
| done→merged | `line merge --confirm` | 干净、base 分支存在、`--confirm` |
| draft/active/blocked→abandoned | `line abandon` | reason |
| done→active（回退） | `line revert` | 目标有 checkpoint、非 merged |

### task 迁移

| 迁移 | 命令 | 前置 |
|---|---|---|
| pending→running | `task start` | 所有 depends 为 done；组外无 running |
| running→done | `task done` | 有 checkpoint、代码干净、line active |
| running→failed | `task fail` | reason |
| failed→running | `task retry` | — |
| running→blocked | `task block` | reason |
| blocked→running | `task unblock` | — |

非法迁移退 2，错误里带 `next`。

---

## 7. 并行组与 join（fork/join）

```bash
# T1 完成后，T2/T3 同属 G1，并行
sele task add "单测" --modules auth --group G1 --depends T1
sele task add "文档" --modules docs --group G1 --depends T1

# join 点：依赖组内全部成员
sele task add "联调" --modules core --depends T2 --depends T3

sele line check <id>     # 组内 depends 必须一致；模块重叠只告警
sele task start T2
sele task start T3       # 同组可同时 running
sele task start T4       # 退 2：T2/T3 未 done
```

---

## 8. 多线并行（worktree）

```bash
git switch main                          # 主工作树先离开该分支
sele worktree add <line> --path ../wt-1  # 独立目录检出 sele/<line>
cd ../wt-1
sele --root . line show <line>           # 在 worktree 内独立运行
sele worktree remove <line>              # 回到主工作树执行
```

worktree 内的 `.sele/runtime/` 是本机新建的，会话指针与 task 运行态按工作树独立；`.sele/lines/` 随分支内容。

---

## 9. 回退与同步

```bash
sele line revert <id> --to T1   # 保留 T1 done，T2 起重置 pending（reset --hard）
sele task revert T2             # 撤销 T2 本身（回到 T1 完成时）
sele task revert T1             # 根任务：整条线回到 draft，需重新 line open
sele line rebase <id>           # 变基到 main
# 冲突时：git status 看冲突文件 → 解决 → git add <file>
sele line rebase <id> --continue
sele line rebase <id> --abort   # 或放弃
```

revert 会 `git reset --hard`，只用于未合并的线；merged/abandoned 只读。

---

## 10. JSON 契约

成功输出为单对象（列表命令为 `{items:[...]}`），通用字段：

| 字段 | 含义 |
|---|---|
| `command` | 如 `line.open`、`task.done` |
| `changed` | 是否真的写盘（false = 幂等） |
| `line` / `task` / `acceptance` | 相关 id |
| `path` | 受影响文件 |

错误输出到 stderr：

```json
{"error":"...","suggestion":"...","next":["sele task start 2026...-fix-login T2"]}
```

`next` 是可直接执行的命令列表，失败时优先照它执行。

退出码：

| 码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 用法错误（缺参数、非法 slug/id） |
| 2 | 状态冲突（非法迁移、line 不存在、git 不干净、冲突） |
| 3 | `line check` / `fsck` 未通过 |
| 4 | 环境错误（无 `.sele`、无 git、IO/JSON 失败） |

---

## 11. 常见错误对照

| 错误 | 处理 |
|---|---|
| `No active line for session` | `sele line list` → `sele line switch <id>` |
| `Working tree has uncommitted code changes` | 先 `sele task checkpoint <line> <task>` 或提交 |
| `Task T2 has no checkpoint` | `sele task checkpoint <line> T2` 后再 done |
| `Dependency T1 is pending, not done` | 先完成 `T1` |
| `Another task is already running: T1` | 同组才能并行；先 `sele task done` 或换组 |
| `Line ... is draft, not active` | `sele line open <id>` |
| `merge requires --confirm` | 加 `--confirm` |
| `Rebase still has conflicts` | 解决 → `git add` → `--continue` |
| `Line ... is archived and read-only` | 归档线不可写；需要新线 |
| `No .sele directory found` | `sele init` |

---

## 12. 双模式与旧体系

- 检测顺序：先向上找 `.sele/`，再找 `.design/`。
- `.sele/` 项目可用：`line` / `task` / `acceptance` / `overview` / `log` / `fsck` / `worktree` / `version`。
- `.design/` 项目可用旧命令：`new` / `append-decision` / `status` / `lint` / `close` / `version`。
- 两者同时存在时旧命令仍可用（迁移期）。
- 在新项目里跑旧命令或用 `status` 会退 4 并提示替代命令；旧体系用 `sele status`，新体系用 `sele overview`。

---

## 13. agent 操作守则

1. 只通过 `sele` 改 `.sele/`；不要手改 `line.json`、`tasks/*.json`、`session` 指针。
2. 每次写操作检查退出码；失败优先执行 `next`。
3. 任何 `task done` 之前先 `task checkpoint`；否则退 2。
4. `line done` 前确认 acceptance 全部 pass（`line show` 可看）。
5. 含空格/中文的参数用单引号包裹；避免全角引号。
6. 多 agent 并行时给每个 agent 不同的 `--session` 或独立 worktree。
7. 不确定当前状态时先跑 `sele overview --output json`，不要猜。
8. 归档线只读；需要继续工作就新建 line。
