# Epic 设计

> 目标版本：0.12.0。本文是完整设计，实现以本文为准。
> Epic 是「长期目标容器」：它跨多条 line，本身不建分支、不做验收收敛；line 仍是执行与合并单元。

---

## 1. 定位与目标

| 概念 | 生命周期 | 验收 | 分支 | merge |
|---|---|---|---|---|
| epic | 长期（可数月） | 无（由子 line 承担） | 无 | 无 |
| line | 一次收敛 | 有 | `sele/<id>` | 有 |
| task | line 内 | 无 | 同 line | 随 line |

目标：

- 让「重构认证体系」这类长期目标有稳定的归属，而不用把一条 line 拖到无法收口。
- epic 只做**汇总与分组**，不复制 line 的状态，避免第二套真相源。
- 与现有 line/task/merge/archive 完全兼容；不改变 line 的任何既有语义。

非目标（明确不做，见 §16）：

- epic 级 acceptance / milestone / task；
- epic 分支、merge、rebase；
- epic 级 decision（决策历史另做或写 docs）；
- epic 归档目录、跨仓库 epic。

---

## 2. 概念与层级

```
epic auth-overhaul（长期目标）
 ├── line 20261030-oauth（active，有验收、有分支）
 │    ├── task T1 → checkpoint
 │    └── task T2 → checkpoint
 ├── line 20261105-session-migration（draft）
 └── line 20261020-login-cleanup（merged，已归档）
```

- 一条 line **最多属于一个 epic**，也可以不属于任何 epic。
- 一个 epic **可以没有 line**（先建目标，后续挂线）。
- epic 不感知 line 的 task/checkpoint；只读取 line 的状态做汇总。

---

## 3. 数据模型

### 3.1 `.sele/epics/<epic-id>/epic.json`（tracked）

```json
{
  "schema_version": 1,
  "version": 3,
  "id": "auth-overhaul",
  "goal": "重构认证体系，支持 OAuth 与 session 迁移",
  "status": "active",
  "created_at": "2026-10-30T08:00:00Z",
  "created_by": "zxionf",
  "closed_at": null,
  "abandoned": null
}
```

| 字段 | 约束 |
|---|---|
| `schema_version` | 固定 1 |
| `version` | 每次写 +1，用于乐观并发 |
| `id` | `^[a-z][a-z0-9-]{2,40}$`，全局唯一，**不带日期** |
| `goal` | 非空，≤200 字 |
| `status` | `active` / `done` / `abandoned` |
| `created_at` / `created_by` | 创建时间与操作者 |
| `closed_at` | `done` 时写入，否则 null |
| `abandoned` | `abandoned` 时 `{reason, at}`，否则 null |

### 3.2 `line.json` 新增字段

```json
"epic": "auth-overhaul"
```

- 类型：`string | null`；缺失按 `null` 处理。
- 写入方式：`line new --epic <id>` 或 `line amend --epic <id|none> --reason`。
- `line archive` 不修改该字段；归档线仍保留所属 epic。

---

## 4. 目录布局

```
.sele/
├── epics/
│   └── <epic-id>/
│       └── epic.json          # tracked
├── lines/
│   └── <line-id>/
│       └── line.json          # 含 "epic"
├── archive/
│   └── <line-id>/line.json    # 归档线保留 "epic"
├── log.jsonl                  # 新增 epic.* / line.epic 事件
└── runtime/                   # 不变
```

- epic 不写 `.sele/runtime/`；状态全在 `epic.json`（低频写入）。
- `epics/<id>/` 目录预留给未来附加文件（如设计说明），当前只放 `epic.json`。

---

## 5. 状态机

```
active ──close──> done ──reopen──> active
   └──abandon──> abandoned
```

| 迁移 | 命令 | 前置 |
|---|---|---|
| （无）→ active | `epic new` | id 合法且唯一 |
| active → done | `epic close` | 所有子 line 状态 ∈ {done, merged, abandoned}；空 epic 允许；`--force` 可跳过 |
| done → active | `epic reopen --reason` | 有 reason |
| active → abandoned | `epic abandon --reason` | 有 reason |
| active（内容） | `epic amend --goal --reason` | 有 reason，goal 合法 |

- `done` 不能直接 `abandon`，需先 `reopen`。
- `abandoned` 是终态，不可再改（只读）。
- epic 没有 `blocked`：子线的 blocked 不上升为 epic 状态，只在 `epic show` 中显示。

---

## 6. 关联规则

1. `line new --epic <id>`：epic 必须存在且 `status=active`，否则退 2。
2. `line amend --epic <id>`：line 为 `draft` / `active` / `blocked`；目标 epic 必须 `active`。
3. `line amend --epic none`：解除关联；`--reason` 必填。
4. `epic close` 之后，不能再有 line 关联到它；需要先 `epic reopen`。
5. 一条 line 只能有一个 epic；重复关联同一个 epic 是幂等操作（`changed:false`）。
6. 归档线仍保留 epic 字段；`epic show` 把它算作“历史”，不计入进行中。
7. epic 的存在不阻止 line 的 `open` / `done` / `merge` / `archive`；所有规则仍以 line 为准。

---

## 7. 命令规格

### 7.1 `sele epic new <id> --goal "..."`

- 校验 id 格式与唯一性（`epics/<id>/epic.json` 不存在）。
- 创建 `epics/<id>/`，写 `epic.json`（`version=1`，`status=active`）。
- 写 `log.jsonl` 的 `epic.new`。
- 输出：`{command:"epic.new", changed:true, epic, status:"active", path}`。
- 失败：id 非法 1；已存在 2；写盘失败 4。

### 7.2 `sele epic list [--status active|done|abandoned]`

- 扫描 `epics/*/epic.json`，按 id 排序。
- 每条附带子线计数：`lines:{total,active,blocked,done,merged,abandoned,archived}`。
- 输出 `{command:"epic.list", items:[...]}`。

### 7.3 `sele epic show <id>`

- 输出 epic 本身、子线清单（id/status/goal）、进度汇总、阻塞项（blocked/active 子线）。
- 子线来源：`lines/` 与 `archive/` 中 `epic == id` 的 line。
- JSON 字段：

```json
{
  "command": "epic.show",
  "epic": {"id":"...","goal":"...","status":"...","created_at":"...","closed_at":null},
  "lines": [{"id":"...","status":"active","goal":"..."}],
  "summary": {"total":3,"active":1,"blocked":0,"done":0,"merged":1,"abandoned":0,"archived":1}
}
```

- 不存在退 2。

### 7.4 `sele epic amend <id> --goal "..." --reason "..."`

- 只允许 `active`；`done` 需先 `reopen`；`abandoned` 退 2。
- goal 非空、≤200 字；reason 必填。
- 写 `log.jsonl` 的 `epic.amend`（含 `old_goal`、`new_goal`、`reason`）。
- 不改子线、不改 line 的 frozen。

### 7.5 `sele epic close <id> [--force]`

- 前置：`status=active`；所有子线状态 ∈ {done, merged, abandoned}。
- 空 epic 允许直接 close。
- `--force`：跳过子线检查（仍有 active/blocked 子线也能关），输出 `forced:true`。
- 写 `status=done`、`closed_at`；`log.jsonl` 的 `epic.close`。
- 失败：已有 done 幂等（`changed:false`）；abandoned 退 2。

### 7.6 `sele epic reopen <id> --reason "..."`

- 前置：`status=done`；reason 必填。
- 写 `status=active`、清 `closed_at`；`log.jsonl` 的 `epic.reopen`。
- active 幂等（`changed:false`）。

### 7.7 `sele epic abandon <id> --reason "..."`

- 前置：`status=active`；reason 必填。
- 写 `status=abandoned`、`abandoned={reason,at}`；`log.jsonl` 的 `epic.abandon`。
- `done` 退 2（先 reopen）；abandoned 幂等。

### 7.8 line 命令扩展

| 命令 | 变化 |
|---|---|
| `line new ... [--epic <id>]` | 写入 `line.json.epic`；校验 epic 存在且 active |
| `line list [--epic <id>] [--all]` | 按 epic 过滤；items 增加 `epic` 字段 |
| `line show` | 输出 `epic` 字段 |
| `line amend ... [--epic <id\|none>] --reason` | 改关联；记录 `line.epic` 日志（old/new） |
| `line archive` | 不修改 epic 字段 |

`line new --epic` 的失败：epic 不存在或非 active → 退 2，`next` 提示 `sele epic list`。

---

## 8. 输出契约

- 所有命令支持 `--output text|json`。
- 写命令的 JSON 通用字段：`command`、`changed`、`epic`、`path`（`epic.close` 等）。
- `epic.list` / `epic.show` 的 items 结构与 §7 一致。
- 错误沿用 `{error, suggestion, next}`；退出码 0/1/2/3/4 不变。
- `overview` 增加：
  - `epics`: `[{id, goal, status, lines:{...}}]`
  - `items[].epic`: line 的所属 epic（无则 null）
  - `overview --epic <id>`：只显示该 epic 的线。

---

## 9. 审计

| 事件 | 文件 | 内容 |
|---|---|---|
| `epic.new` | `log.jsonl` | id、goal |
| `epic.amend` | `log.jsonl` | old_goal、new_goal、reason |
| `epic.close` | `log.jsonl` | forced |
| `epic.reopen` | `log.jsonl` | reason |
| `epic.abandon` | `log.jsonl` | reason |
| `line.epic` | `log.jsonl` | old、new（关联变更） |

`line.epic` 只在关联真的变化时写入。

---

## 10. 一致性检查（fsck）

新增检查：

| 检查 | 严重度 |
|---|---|
| `epic.json` 可解析、schema/id/goal/status 合法 | 失败(3) |
| `line.json.epic` 指向的 epic 存在 | 失败(3) |
| epic `status=done` 但仍有 active/blocked 子线 | 失败(3) |
| epic `status=done` 但 `closed_at` 为空 | 失败(3) |
| epic `status=abandoned` 但 `abandoned` 为空 | 失败(3) |
| 归档线引用的 epic 不存在 | 警告 |

`line check` 不检查 epic（line 自身检查不变）。

---

## 11. 与 git 的关系

- epic **不建分支、不产生 commit**；`epic.json` 是普通 tracked 文件。
- epic 的修改（new/amend/close/reopen/abandon）只写 `.sele/epics/` 与 `log.jsonl`，不自动提交；随下一次 `task checkpoint` / `line done` 或手动提交入库。
- 子 line 照常走 `sele/<id>` 分支、checkpoint、merge；epic 不参与。
- 归档线仍保留 `epic` 字段，历史可追溯。

---

## 12. 归档与历史

- epic 本身不归档（没有 `epic archive`）；关闭/放弃后保留在 `epics/`。
- line 归档（`line archive`）只移动 `lines/<id>` → `archive/<id>`，`epic` 字段保留。
- `epic show` 同时扫描 `lines/` 与 `archive/`：
  - `archived` 计入 `summary.archived`，不计入进行中；
  - 归档线的状态以 `line.json.status` 为准（通常 merged/abandoned）。

---

## 13. 兼容与迁移

- 旧 line（无 `epic` 字段）：按 `null` 处理，不需要迁移。
- 旧 `.sele` 没有 `epics/` 目录：`sele init` 的 repair 模式会创建空目录；命令按需创建。
- `epic` 字段是新增字段，不提升 `schema_version`。
- 版本：0.12.0（新增功能，向后兼容）。

---

## 14. 测试计划（约 14 条）

| # | 用例 |
|---|---|
| 1 | `epic new` 创建文件、日志、active |
| 2 | `epic new` 重复 id / 非法 id |
| 3 | `epic new` goal 为空或超长 |
| 4 | `epic list` 列出与 `--status` 过滤 |
| 5 | `line new --epic` 关联成功，`line.json.epic` 正确 |
| 6 | `line new --epic` 指向不存在/已关闭 epic 退 2 |
| 7 | `line amend --epic` 改关联、`none` 解除、记录日志 |
| 8 | `line list --epic` 过滤 |
| 9 | `epic show` 汇总子线（含归档线）与阻塞项 |
| 10 | `epic close` 有 active 子线退 2；`--force` 成功 |
| 11 | `epic close` 全部子线结束后成功；`closed_at` 写入 |
| 12 | `epic reopen` 后子线可重新关联 |
| 13 | `epic abandon` 与幂等；`done` 不可 abandon |
| 14 | `fsck` 检测 epic 引用缺失/关闭不一致 |
| 15 | `overview` 的 `epics` 与 `items[].epic` |
| 16 | 归档线仍计入 `epic show` 的历史 |

---

## 15. 实现落点

```
src/core/
├── model.hpp/.cpp       # Epic 结构、序列化、id 校验
├── sele.hpp/.cpp        # layout 加 epics/；读取 epic
└── (复用 fs/hash/journal)

src/cli/
├── epic.cpp             # epic new/list/show/amend/close/reopen/abandon
├── line.cpp             # line new --epic、line amend --epic、line list --epic、line show
├── overview.cpp         # epics 汇总、items[].epic、--epic 过滤
└── init.cpp             # 创建 epics/ 目录（repair 也补）

tests/sele_tests.sh      # 16 条新用例
docs/
├── sele-v1-spec.md      # 新增 §epic
├── sele-v0.10.0.md      # 命令表补 epic
└── sele-user-guide.md   # 人类使用补一节
```

---

## 16. 非目标

| 项 | 原因 |
|---|---|
| epic 级 acceptance / milestone | epic 不收敛；验收放在子 line |
| epic 分支 / merge / rebase | 长期容器不应占用分支 |
| epic 级 task | task 属 line |
| epic 级 decision | 决策历史独立处理（`sele decision` 或 docs） |
| epic 归档 | 关闭/放弃后保留即可 |
| 跨仓库 epic | `.sele/` 单仓库边界 |
| epic modules | 模块是 line 级；epic 只按 goal/id 区分 |

---

## 17. 风险与对策

| 风险 | 对策 |
|---|---|
| epic 变成第二套状态机 | 只做汇总；不引入 task/acceptance/分支 |
| `epic show` 扫描全量 lines 变慢 | 子线数量可控；未来可加索引，但先不做 |
| epic close 后仍有 active 子线 | `close` 前置校验；`--force` 显式跳过并记 `forced` |
| 归档线遗漏 | `epic show` 同时扫描 `archive/`；`fsck` 校验引用 |
| 旧 config/旧 line 不兼容 | `epic` 缺失按 null；repair 创建 `epics/` |

---

## 18. 验收标准

1. `sele epic new/list/show/amend/close/reopen/abandon` 全部可用，JSON 字段符合 §7。
2. `line new --epic` 与 `line amend --epic` 能建立/解除关联，`line list --epic` 可过滤。
3. `epic close` 在子线未结束且无 `--force` 时退 2；`--force` 成功并标记 `forced`。
4. `epic show` 汇总包含 `lines/` 与 `archive/`，阻塞项可见。
5. `fsck` 能检测 epic 引用缺失与关闭不一致。
6. `overview` 输出 `epics` 与 `items[].epic`。
7. 现有 63 条测试不回归；新增约 16 条通过。
8. 版本 0.12.0；构建无警告；`docs/` 同步。
