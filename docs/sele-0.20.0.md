# sele 0.20.0 实现说明

> 本文描述 0.20.0 的实际实现。设计依据是 `docs/sele-f.md`（接口契约）与
> `docs/sele-f-d.md`（概念模型）。0.20.0 不兼容 0.12 及更早版本：旧命令、旧
> `.sele` 结构、旧文档全部不再支持。

## 1. 定位

`sele` 是约束型 agent 的任务编排与进度账本：

- 四层：`Plan → Line → Task → Step`。
- 只记录，不调度：所有状态迁移都由显式命令触发。
- 三个不变量：
  - **I1** 全局至多一个 `in_progress` task；
  - **I2** 单 task 内至多一个 `in_progress` step；
  - **I3** 任何状态迁移都写一条 `log.jsonl` 事件。

## 2. 目录布局

```
.sele/
├── config.json
├── plans/<plan-id>.json
├── lines/<plan-id>.<line-id>.json
├── tasks/<sha256(uid)[:2]>/<sha256(uid)[2:16]>.json
├── checkpoints/<yyyy-mm>/<ck-id>.json
├── log.jsonl
├── archive/
└── cache/context.json
```

- Task 文件按不可变 `uid`（`tsk_` + ULID）哈希分桶，改名/换线不改文件名。
- Line 文件名带 plan 前缀，避免跨 plan 重名。
- `log.jsonl` append-only；`cache/` 不进版本控制。

## 3. 命令

### 3.1 初始化与版本

| 命令 | 说明 |
|---|---|
| `sele init [--root <dir>]` | 创建 `.sele/` 布局 |
| `sele version` | 打印版本 |
| `sele --version` | 同上 |

### 3.2 Plan

| 命令 | 说明 |
|---|---|
| `plan new <id> --goal <g> [--acceptance <desc>]... [--lines a,b]` | 建 draft plan |
| `plan submit <id>` | draft → pending_approval（至少一条 line） |
| `plan approve <id> [--by <who>]` | pending_approval → active |
| `plan reject <id> --reason <r>` | pending_approval → draft |
| `plan status <id>` | 状态 + line 计数 |
| `plan show <id>` | 完整 JSON |
| `plan verify <id> <PA-id> [--by <who>]` | 验收点置 pass |
| `plan done <id>` | active → done（line 全结束 + 计划验收全 pass） |
| `plan archive <id>` | done → archived |
| `plan restore <id>` | archived → active |
| `plan list [--status <s>]` | 列表 |

### 3.3 Line

| 命令 | 说明 |
|---|---|
| `line new <id> --plan <p> --goal <g> --modules a,b [--acceptance <desc>]...` | 在 active plan 下建线（active） |
| `line activate <id> --plan <p>` | draft → active |
| `line switch <id> [--plan <p>]` | 写上下文 |
| `line status <id> --plan <p>` | 线 + task 摘要 |
| `line verify <id> <A-id> [--by <who>] --plan <p>` | 验收点置 pass |
| `line done <id> --plan <p>` | active → done（task 全结束 + 验收全 pass） |
| `line abandon <id> --reason <r> --plan <p>` | 放弃 |
| `line list [--plan <p>] [--status <s>]` | 列表 |
| `line dep-add <id> --plan <p> --type line --target <t>` | 加依赖（拒绝环） |
| `line dep-remove <id> --plan <p> --target <t>` | 删依赖 |
| `line check [--plan <p>] [--id <line>]` | 依赖环检查 |

### 3.4 Task

| 命令 | 说明 |
|---|---|
| `task add "<title>" --line <id> [--plan <p>] [--modules a,b] [--depends T1]... [--acceptance <desc>]... [--review-policy strict\|lenient\|mixed]` | 建 task（todo） |
| `task start <id> [--line <l>] [--plan <p>]` | todo → in_progress；自动暂停当前 in_progress（I1） |
| `task pause <id> --reason <r> [--hint <h>]` | in_progress → paused，写 `pause_context` |
| `task resume <id>` | paused → in_progress |
| `task done <id>` | step 全 done/skipped；验收未满足 review_policy 时转 `pending_review` |
| `task approve <id>` | pending_review → done |
| `task fail <id> --reason <r>` | in_progress → failed |
| `task retry <id>` | failed → in_progress |
| `task block <id> --reason <r>` | in_progress → blocked |
| `task unblock <id>` | blocked → in_progress |
| `task verify <id> <A-id> [--by <who>]` | 验收点置 pass |
| `task archive <id>` | done → archived |
| `task revive <id>` | archived → todo |
| `task list [--plan <p>] [--line <l>] [--status <s>]` | 列表 |

### 3.5 Step

| 命令 | 说明 |
|---|---|
| `step add <task> "<title>" [--after <S>] [--requires-review] [--line <l>]` | 追加/插入 step |
| `step start <task> <step>` | 检查 I2，设置 `current_step` |
| `step done <task> <step> [--checkpoint <ck>]` | 需审核时转 `pending_review` |
| `step approve <task> <step>` / `step reject <task> <step> --reason <r>` | 审核 |
| `step skip <task> <step> --reason <r>` | → skipped |
| `step goto <task> <step> --reason <r>` | 跳转；中间 todo/in_progress 标 skipped |
| `step list <task>` | 列表 |

### 3.6 查询

| 命令 | 说明 |
|---|---|
| `next [--plan <p>] [--line <l>] [--resume]` | 就绪 task，拓扑序 |
| `status [--plan <p>]` | plan/line/task 状态摘要 |
| `tree [--plan <p>] [--all]` | 四层结构视图 |
| `log [--plan <p>] [--line <l>] [--task <t>] [--decisions] [--limit <n>]` | 事件日志 |
| `check [--plan <p>]` | 一致性检查（引用、依赖环、done 一致性、日志可解析） |

### 3.7 Git 代理

| 命令 | 说明 |
|---|---|
| `git status [--short]` | 透传 `git status` |
| `git diff [--stat]` | 透传 `git diff` |
| `git log [--limit <n>]` | 透传 `git log --oneline` |
| `git commit --task <t> --step <s> --message <m> [--files a,b]` | 校验状态 → `git add` → `git commit` → 生成 checkpoint → 写 `git.commit` 事件 |

## 4. 状态机

### Plan

```
draft → pending_approval → active → done → archived
  ↑__________________________|        |
        reject                        └→ aborted? (未实现)
```

### Line

```
draft → active → done
          ├→ blocked → active
          └→ abandoned
```

### Task

```
todo → in_progress → done → archived → todo
         ├→ pending_review → done
         │                 └→ in_progress
         ├→ paused → in_progress
         ├→ blocked → in_progress
         └→ failed → in_progress
```

### Step

```
todo → in_progress → done
         ├→ pending_review → done
         │                 └→ in_progress
         ├→ blocked → in_progress
         ├→ failed → in_progress
         └→ skipped
```

### 关键前置

| 迁移 | 前置 |
|---|---|
| `task start` | depends 全 done/archived；全局无其他 in_progress（有则自动 pause） |
| `step start` | task 为 in_progress；同 task 无其他 in_progress step（I2） |
| `task done` | 所有 step done/skipped；验收按 review_policy（未满足转 pending_review） |
| `line done` | 所有 task done/archived；line 验收全 pass |
| `plan done` | 所有 line done/abandoned；plan 验收全 pass |
| `plan submit` | 至少一条 line |

## 5. 验收策略

`task.review_policy`：

| 值 | 完成条件 |
|---|---|
| `strict` | 所有验收点 pass 且 `verified_by` 以 `human` 开头 |
| `lenient` | 所有验收点 pass |
| `mixed`（默认） | `requires_human` 的验收点需 human；其余只需 pass |

## 6. 输出与退出码

所有命令支持 `--root` 与 `--output text|json`。

| 退出码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 用法/参数错误 |
| 2 | 状态冲突（非法迁移、依赖未满足） |
| 3 | 一致性检查失败 |
| 4 | 环境/数据错误 |

## 6.1 引用与 uid

`depends` 存储目标 task 的 `uid`（不可变），语义 ID 仅用于命令与展示。
旧格式 `{"type":"task","plan":...,"line":...,"id":...}` 仍可解析（回退）。
全限定引用 `T1` / `line.T1` / `plan.line.T1` 在 CLI 层解析后同样落到 uid。

## 7. 与 sele-f 规范的差异（0.20.0 进度）

已实现 **M1（账本）** 与 **M2（Git 代理）**：

- M1：四层模型、状态机、事件、`next/status/tree/log/check`。
- M2：`sele git status/diff/log` 透传；`sele git commit --task --step --message [--files]`
  校验 task/step 状态、执行 `git add` + `git commit`、生成 checkpoint JSON
  （commit/tree/branch/files_changed/diff_stat），并写 `git.commit` 事件。
- 漂移 D1（HEAD 前进，自动更新指纹 + 写 `git.drift` 事件）、D2（回退阻断）、D3（分叉阻断）；
  D2/D3 时 `sele git commit` 退 4。

未实现（M3/M4）：

- `reconcile`、D4–D6、checkpoint 的 remap/invalidate；
- `task revert`（三档回滚）；
- `cache/index.json` 索引（当前直接扫描目录）；
- `log compact`、`decision record`、`task keep`、`plan switch`、`--full`、`--replay`；
- task 级跨线依赖（当前 `--depends` 限同 line）；
- step 依赖、step 锁、`task insert`、`task move`；
- Plan 的物理归档/恢复（当前只改状态）；
- `sele git reset` 与 `--exec` 回滚。

## 8. 测试

```bash
bash tests/sele_v2_tests.sh build/build/sele   # 16 条
ctest --test-dir build
```

覆盖：初始化、版本、plan 审批、line 前置、task/step 完整流程、I1 自动暂停、
I2 单 step、done 前置、next 依赖、check 引用、log/tree。

## 9. 旧数据

0.20.0 不读取 0.12 的 `.sele` 结构。旧数据（`lines/<id>/`、`tasks/T1.json`、
`epics/`、`session`、旧 `log.jsonl`）保留为只读历史，不参与新命令。
