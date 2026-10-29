# sele-e 设计方案评审

> 评审对象：`docs/sele-e.md`（471 行，v0.1 草案）
> 评审日期：2026-10-29
> 前提（已定）：
> 1. `.sele/` 是**下一代 sele 的工作目录**，地位等同 `.git/`——工具私有、由命令读写、用户不直接编辑。
> 2. `.design/` 与 `.agent/` 是**当前这代 sele 与 agent 的工作空间**，不作为下一代的状态载体。
>
> 本文只评审，不改 `sele-e.md`。

---

## 0. 结论摘要

| 维度 | 评价 | 说明 |
|---|---|---|
| 方向（多线 + 验收 + 显式状态 + 审计） | **合理** | 补上现有 sele 最缺的三块：可判定验收、状态机、并行表达 |
| 概念模型 | 基本成立 | Task 与 commit 的类比需修正；Checkpoint 才是 commit |
| `.sele/` 定位 | **已明确** | 工具内部状态目录，可推出"不可手改、可重建、tracked/runtime 分区"三条硬结论 |
| 状态机与命令面 | **不自洽** | 8 处缺口：`reopen`/`skip` 无命令、blocked 循环定义、验收覆盖不可计算等 |
| 存储与依赖 | 有风险 | YAML 是新依赖；与"JSON + 两个 header-only"的现状冲突 |
| 并发与 git | **关键机制缺失** | merge 冲突、rebase、worktree、锁全部留白 |
| v0.1 范围 | 过大 | 20+ 命令 + worktree + rebase，风险最高处恰恰没细节 |
| 与上一代的关系 | 已定代际，未定共存策略 | 需要回答"单二进制双模式"还是"新二进制分叉" |

一句话：**作为设计草图是合格的；作为实施方案还差一层——`.sele` 的定位解决了"双真相源"问题，但把问题转移到了"两代共存 + git 契约 + 事件一致性"上。**

---

## 1. `.sele/` = `.git/` 这个定位的四个直接推论

这个类比一旦成立，有四条结论是**推导出来的**，不是设计选择：

### 1.1 用户与 agent 都不直接编辑 `.sele/`

`.git/` 的内容只能经 git 命令改动，手改 refs/objects 视为损坏。同理：

- 合理：`line.yaml`、`task.yaml` 由 `sele` 渲染写入，agent 只调命令。
- 不合理（`sele-e.md §5.2` 隐含）：把 `line.yaml` 当成"人工审查与 diff"的入口。人工审查应走 `sele line show` 或 `sele diff`，而不是编辑器。
- 由此需要一项新能力：**`sele fsck`**（校验 `.sele/` 内部一致性、可重建缓存）。`.git` 有 `git fsck`，这是类比的必要配套。

### 1.2 元数据必须是"可重建的投影"，而不是唯一真相

`.git` 的真相是 objects + refs，index 只是缓存（可删可重建）。`.sele` 若把 `line.yaml` 当唯一真相：

- 崩溃时可能半写（需要原子 rename + 版本号）；
- 并发写时后写者覆盖（需要乐观锁）；
- 审计日志与状态可能不一致（需要对账）。

**建议**：`log.jsonl`（append-only）为真相，`lines/**/*.json` 为物化视图，`sele fsck --rebuild` 可从日志重建状态。这是 `.git` 类比最正确、文档完全没提的一点。v0.1 若不做完整事件溯源，至少要做到"先 append 事件、再原子写状态、`fsck` 能对账并报告不一致"。

### 1.3 Worktree 必须外置，`.sele/` 只存指针

`.git` 的 worktree 内容在仓库外（`git worktree add ../path`），`.git/worktrees/<name>/` 只存指针与 HEAD。

`sele-e.md §5.9` 把 `worktree/` 放进 `.sele/lines/<id>/worktree/`，这会导致：

- `git status` 出现嵌套仓库噪音，需要精细 `.gitignore`；
- `sele` 自己的元数据与代码副本混在一起，难以区分 tracked/runtime；
- 删除 line 时可能误删未提交代码。

**建议**：worktree 放在 `.sele/runtime/worktrees/<line-id>.json`（只存 `{path, branch, head_task}`），实际目录放在仓库外或 `.sele/../<repo>-wt-<line>`，与 git 的 worktree 惯例一致。

### 1.4 `.sele/` 内部要分成 tracked 与 runtime 两区

`.git/` 整体不被 git 跟踪，但 `.sele/` 里的目标、验收、模块标签是**人类要 review 的资产**，应当入库；而锁、会话指针、worktree 路径是本机运行态，不应入库。这一点文档没有区分，若照 §5.9 整体入库，会产生大量跨机器冲突。

**建议布局**（`--` 表示建议删除）：

```
.sele/
├── config.json              # tracked: schema_version, actor, modules 白名单, checkpoint 策略
├── log.jsonl                # tracked: 里程碑事件（line.open/line.done/line.merge/line.abandon）
├── lines/
│   └── <line-id>/
│       ├── line.json        # tracked: id/type/goal/acceptance/modules/base/status/owner/created_at
│       ├── tasks/T1.json    # tracked: id/title/depends/modules/acceptance/status/checkpoint
│       └── design.md        # tracked: 人读设计（可选，见 §7 迁移）
├── archive/                 # tracked: merged/abandoned 的 line 目录
└── runtime/                 # gitignore
    ├── events.jsonl         # 细粒度事件（task.start/checkpoint/done）
    ├── sessions/<sid>.json  # 每个会话的当前 line/task（替代全局单指针）
    ├── worktrees/<line>.json
    └── lock
```

对应 `sele-e.md §5.9` 的修改：

| 原设计 | 问题 | 建议 |
|---|---|---|
| `.sele/main/` | 未定义，且 main 在 git 里是分支 | 删除；用 `line.base` + `line.head`（commit） |
| `.sele/config.yaml` | YAML 依赖 | `config.json`（nlohmann 已有） |
| `.sele/lines/<id>/worktree/` | 内嵌工作树 | 移到 `runtime/worktrees/<id>.json` 存指针 |
| `.sele/lines/<id>/checkpoints/` | 与 git commit 重复 | 删除；commit 哈希存 `task.checkpoint.commit` |
| `.sele/log.jsonl`（全局） | 无 tracked/runtime 区分 | 拆 `log.jsonl`（里程碑，入库）+ `runtime/events.jsonl`（细则，忽略） |

---

## 2. 值得保留的五点

| 设计 | 解决的现有缺口 |
|---|---|
| Line 带 `acceptance`，且可判定 | `close` 不检查任务是否完成，未验证的设计可直接进 SPEC |
| Line/Task 双状态机 + 显式迁移 | `STATE.md` 四行自由文本，没有非法迁移概念 |
| 显式 fork/join，禁止隐式并发 | 现有体系对并行零表达 |
| append-only 审计 | `DECISIONS.log` 是人读散文，无法重放；审计日志可以 |
| 多线共存，单指针不再是瓶颈 | 现有 `Active Change` 单指针 + `new` 可静默覆盖 |

"线内串行、线间异步"这条边界也立得住：收益来自线间，可回滚性靠线内。

---

## 3. P0 剩余阻塞项（4 项 + 2 项新增）

### P0-1（新增）两代如何共存

`.sele/` 已定为下一代，但同一个二进制如何对待 `.design/`+`.agent/`，有三种策略，必须选：

| 策略 | 行为 | 代价 |
|---|---|---|
| **A 单二进制双模式**（推荐） | 检测顺序：有 `.sele/` 走新体系；无 `.sele/` 但有 `.design/` 走旧体系并提示；都没有则 `sele init` 让用户选 | 双份代码路径，但迁移平滑，旧项目零改动 |
| B 新二进制分叉（`sele2`） | 两条命令并存 | 用户要装两个，路径/配置容易混 |
| C 一次性迁移 | 新版本只认 `.sele/`，提供 `sele import` 把 change 转 line | 干净，但旧仓库必须先迁移，回退困难 |

无论选哪个，都需要定义旧资产去向：`.design/SPEC.md`、`DECISIONS.log`、3 个 `changes/<id>`。建议提供 `sele import <change-dir>`：`design.md` 落到 `lines/<id>/design.md`，`tasks.md` 的勾选项转成 `task` 记录，`DECISIONS.log` 逐条转成 `log.jsonl` 的 `decision` 事件（保留原文与时间戳），不删除旧目录。

### P0-2 `checkpoint = git commit` 与现行 `CONTRACT.md` 冲突

`.agent/CONTRACT.md` 明文"任何情况下不在本仓库创建 git 提交"。`sele-e.md §8.1` 把 `git-commit` 设为默认策略。这是**契约变更**，必须由用户决定并留决策记录。

连带必须定义：

- `revert` = `git revert`（新增还原 commit）还是 `reset`（改写历史）？建议只允许前者。
- 每 task 一 commit 的 review 成本由谁承担？
- 若用户暂不接受 agent 提交，`snapshot` 策略是什么（复制目录？补丁文件？）——文档列了 `snapshot|manual` 但没定义语义。

### P0-3 `main` 是什么

`base: main`、`.sele/main/`、`sele/<id>` 分支三处出现，未定义。若 main 是 git 分支，则 `line merge` 是真实合并，必须定义：冲突谁来解、在哪个 worktree 解、解完如何继续 checkpoint、`line done` 与 `merge` 之间是否需要人工确认。文档只写了"冲突时 line 回到 active，冲突 task 标记 blocked"，把最难的部分留空了。

### P0-4 acceptance 由谁执行

`check: "pytest tests/..."` + `line done` 时"跑 acceptance"引入任意命令执行器（超时、环境、注入、跨平台 shell），与"agent 不自由执行"的边界冲突。

建议：**工具只记账，不执行**。

```
sele acceptance pass A1 --evidence "pytest 12 passed" --by agent
sele acceptance fail A2 --reason "..."
```

`line done` 的前置条件变成"所有 acceptance 有 pass 记录"，既避免执行器，又保留可审计。

### P0-5（新增）`.sele` 与版本控制的分区

哪些文件入库、哪些忽略、冲突怎么办，必须在 v0.1 前定死（见 §1.4）。尤其 `log.jsonl` 若入库，多人并行会产生行级冲突；若不入库，审计会随机器丢失。两个可选方案：

- **log 入库**：冲突用"行拼接"解决（JSONL 天然可合并），但需要 `.gitattributes` 标记 `merge=union`。
- **log 不入库**：只把里程碑事件写入 `lines/*/line.json` 的 `history` 数组（入库），细则留 runtime。

推荐后者：入库的是"状态 + 里程碑"，可读可 review；细粒度审计留在本机。

### P0-6（新增）命令兼容与命名冲突

- 上一代 `sele status` = 当前变更状态；新一代 = 全局概览。同名列语义变化，属破坏性变更，需要版本前缀或新名字（`sele overview`）。
- 上一代草案 `20261029-write-path-commands` 里的 `sele task done <n>` 与新一代 `sele task done <task-id>` 冲突。
- 建议：新一代命令面整体走 `sele line ...` / `sele task ...`，但 `status` 保留旧语义（单线摘要），全局概览叫 `sele overview`。

---

## 4. `.git` 类比的成立与不成立

| 类比点 | 是否成立 | 说明 |
|---|---|---|
| 工具私有目录，命令写入 | ✅ | 推论见 §1.1 |
| 内容寻址、不可变对象 | ❌ | `line.json` 可变；若要真正类比，需 `log.jsonl` 为真相、JSON 为投影（§1.2） |
| `HEAD` 单指针 | ⚠️ | git 在多 worktree 下每个工作区一个 HEAD；`.sele` 也应用 `runtime/sessions/<sid>.json` 而非全局指针 |
| `refs/` 一行一哈希 | ✅ | `line.head` 存 commit 哈希即可 |
| `logs/HEAD`（reflog）只追加 | ✅ | `log.jsonl` 同理；但 git 的 reflog 不入库 |
| `index` 暂存区 | ❌ | 无需对应 |
| 工作树在仓库外，元数据只存指针 | ❌（文档现状） | `sele-e.md` 把 worktree 内嵌，应改为外置（§1.3） |
| `git fsck` 可校验与重建 | ❌（文档缺失） | 必须补 `sele fsck` |
| `user.name/email` 作为 actor | ✅ | 复用 `git config user.name`，避免再要一套身份 |
| plumbing / porcelain 分层 | ❌（文档缺失） | 建议所有命令支持 `--json`，作为 agent 的 plumbing |

结论：**`.sele` 更像 `.git` 的"元数据 + reflog"部分，不像 objects 部分**。这决定了它应该轻、可重建、分区明确，而不是一个不断增长的数据库。

---

## 5. P1：文档内部不自洽（实现前必改）

| # | 位置 | 问题 | 建议 |
|---|---|---|---|
| 1 | §4.1 表 vs §6.1 | 提到 `done→reopen`、`failed→skip`，命令表无 `reopen` / `skip` | 补命令，或删迁移 |
| 2 | §4.1 `active→blocked` | 条件"存在 blocked 且阻塞性 task"用 blocked 定义 blocked | 改为"存在 status=blocked 的 task，或存在未解决跨线冲突" |
| 3 | §2 概念表 | Task 与 Checkpoint 都类比 commit | Task 是工作单元，Checkpoint 才是 commit |
| 4 | §5.3 depends | "为空表示依赖前一个线性 task"是隐式语义，插入/重排会静默改变依赖 | 显式声明前驱；空 = 无依赖（仅根 task），并规定 line 追加式、不允许中间插入 |
| 5 | §5.5 孤儿 task | 若允许多根，多个空 depends 都被判"不可达" | 规定"恰好一个根，所有 task 从根可达"，写成 check 第 0 项 |
| 6 | §5.5 验收可达性 | `task.yaml` 无 `acceptance` 字段，覆盖关系无法计算 | task 增加 `acceptance: [A1]` |
| 7 | §5.4 R6 | "failed → join 阻塞 → line blocked"与 line 的人工阻塞混淆 | 区分 `blocked`（人工）与 `waiting`（自动），或规定 failed 不改 line 状态 |
| 8 | §5.4 R3 | 组内 modules 重叠即报错，但每线独立 worktree，物理不冲突 | 降级为警告，merge 期再阻断 |
| 9 | §8.3 | "状态锁"与"乐观锁 + 版本号"并存，实现方式未定；孤儿锁与 Windows 文件锁未讨论 | v0.1 用"版本号比对 + 原子 rename"，不引入锁文件 |
| 10 | §8.1 vs §8.3 | worktree 隔离跨线写，但 modules 软锁假设共享写 | 明确：调度期告警、merge 期阻断 |
| 11 | §5.9 | `.sele/main/` 无定义 | 删除（见 §1.4） |
| 12 | §5.2 | id 由人给，多 agent 会撞名 | 沿用 `<YYYYMMDD>-<slug>`，或加 owner 段 |
| 13 | §5.2 | 缺 `schema_version`、`head`、`worktree` | 补字段，否则 rebase/revert 无从下手 |
| 14 | §8.1 | 引入 YAML 新依赖，且隐式类型有坑（`on`/`NO`/日期） | 元数据用 JSON，`sele line show` 渲染成 YAML/Markdown |
| 15 | §5.2 modules | 自由标签，`auth` 与 `authentication` 无法判等 | `config.json` 维护合法模块表，`line check` 校验标签已注册 |
| 16 | §7.1 | agent 不能 `line new`，但人类可能不在线 | agent 可建 `draft`，`draft→active` 需审批；门禁放在激活而非创建 |
| 17 | §6.1 `line switch` | 有"当前线"概念但未定义存哪；多 agent 时全局指针又是瓶颈 | 每会话一个指针（`runtime/sessions/<sid>.json`） |
| 18 | §5.8 revert | 未区分 revert（新增还原 commit）与 reset（改写历史） | 只允许 revert，并记录到 log |
| 19 | §8.2 校验引擎 | 用 JSON Schema 校验 YAML | 若改 JSON，可直接用 nlohmann 的 `json::parse` + 手写 schema 检查，避免新依赖 |
| 20 | §5.3 / §8.4 | `expected_output` 里既有文件路径又有 `tests/x.py::test_y` 这种测试选择器，“存在性检查”无法统一 | 拆成 `outputs`（路径 glob，可检查）与 `acceptance`（验收，见 P0-4） |
| 21 | §4.1 / §5.2 | `draft` 可编辑 goal/acceptance，但 `active` 后无冻结规则，验收标准可被 agent 事后改写 | `draft→active` 时对 goal/acceptance 取哈希写入 `line.frozen`；`active` 后修改需显式 `line amend` 并留审计事件 |

---

## 6. P2：v0.1 范围建议收缩

`sele-e.md` 的 v0.1 列了 20+ 命令、6 项图检查、worktree、rebase、revert、archive、acceptance 执行、JSON Schema。相对当前 1700 行的代码量是 5~10 倍扩张，而风险最高的三块（worktree、merge/rebase、并发）没有细节。

建议 v0.1 = **多线共存、单线单写者**：

| 保留 | 延后 |
|---|---|
| `line new/list/status/check/done/abandon`、`task add/start/done/fail/block`、`overview`、`log`、`acceptance pass/fail` | `fork/join`（先线内串行） |
| line/task 状态机 + 迁移校验 | worktree、`line merge/rebase/revert`、`archive` |
| `sele fsck`、版本号乐观锁、原子写 | 跨线 modules 软锁（只做 `line check` 告警） |
| `log.jsonl`（里程碑）+ `runtime/events.jsonl` | 事件溯源重建（`fsck --rebuild` 可后续加） |
| 与旧 `.design/` 单向引用 / `sele import` | 旧体系被渲染取代 |

理由：多线异步的收益是"不互相阻塞、可随时切换、状态不互踩"，不要求物理同时写代码。先验证状态机与审计是否被 agent 真正使用，再承担 git 工作树的复杂度。

---

## 7. 对当前进行中的变更的影响

`.design/changes/20261029-write-path-commands`（active，tasks 0/14）里的：

| 任务 | 在新体系下 |
|---|---|
| `core/fs` 原子写 | **仍然需要**（写 JSON/事件的基础设施） |
| `core/state`（Markdown STATE 行编辑） | 不需要（状态改为 `line.json` + 会话指针） |
| `sele task add/done/undone/list`（序号语义） | **被取代**（新语义是 task-id + 状态机） |
| `sele handover write/show` | 被 `checkpoint` / `log` 取代 |
| `append-decision --from-file` | 仍可能需要（决策入 log） |

建议：把该变更收缩为"仅 `core/fs`"，或直接 `abandon`，把原子写与版本比对并入新一代的基础设施变更。不要在旧命令面上继续投入测试。

---

## 8. 待决策清单

| 编号 | 决策点 | 选项 | 建议 |
|---|---|---|---|
| D1 | 两代共存方式 | 单二进制双模式 / 新二进制分叉 / 一次性迁移 | 单二进制双模式 + `sele import` |
| D2 | agent 能否创建 git commit | 可以 / 不可以 / 仅特定 line | 允许，但每次 checkpoint 写一条审计事件 |
| D3 | `main` 的定义 | git 分支 / 逻辑基线 | git 分支，merge 是真实合并 |
| D4 | acceptance 执行 | 工具执行 / 工具记账 | 记账（`acceptance pass/fail --evidence`） |
| D5 | 元数据格式 | YAML / JSON | JSON（避免新依赖与 YAML 类型坑） |
| D6 | `.sele` 入库范围 | 全部 / 状态+里程碑 / 全部忽略 | 状态+里程碑入库，runtime 忽略 |
| D7 | 真相来源 | JSON 文件 / `log.jsonl` | `log.jsonl` 为真相、JSON 为投影（可分期） |
| D8 | v0.1 是否含 fork/join 与 worktree | 是 / 否 | 否，先"多线共存、单写者" |

---

## 9. 与 `sele-e.md` 逐节对应

| `sele-e.md` 章节 | 评审结论 |
|---|---|
| 一、工具定位 | 方向成立；"提交的是任务而非代码"与 checkpoint=git commit 有张力，建议改述为"任务 + 代码快照" |
| 二、概念模型 | 修正 Task/Checkpoint 与 commit 的类比（§5-3） |
| 三、设计原则 | 7 条均成立；建议补第 8 条"`.sele/` 是工具私有、可重建" |
| 四、状态机 | 迁移表与命令面不自洽（§5-1/2/7）；补 `reopen`/`skip` 或删除 |
| 五、Line/Task 规范 | depends 隐式语义、验收覆盖不可计算、模块标签无白名单、expected_output 混用路径与测试选择器（§5-4/5/6/15/20） |
| 六、CLI 命令 | 缺 `fsck`、`overview`、`acceptance`；`status` 与旧命令冲突（P0-6） |
| 七、Agent 集成 | 开线门禁应放在 `draft→active`（§5-16）；验收标准需在激活时冻结（§5-21） |
| 八、技术实现 | YAML 依赖、锁机制、worktree 内嵌、无 fsck（§1.2/1.3、§5-9/14、P0-5） |
| 九、版本规划 | v0.1 过大，建议收缩（§6） |
| 十、决策汇总 | 与本文 D1–D8 需要合并成一张最终决策表 |
