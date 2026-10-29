# Sele 完整设计方案

## 一、工具定位

**Sele** 是面向约束型 Agent 的命令行编排工具。借鉴 Git 线性提交模型，以 **Line（线）** 作为目标推进单元，把 Agent 行为约束在可验收、可回滚、可审计的线性轨道上，通过多线并行实现异步推进。

**核心公式**：Sele = 给 Agent 用的 Git，提交的是任务而非代码。

## 二、核心概念模型

| 概念 | 定义 | 类比 Git |
|---|---|---|
| Line | 目标导向的推进单元，有明确验收标准 | branch |
| Task | 线上可执行、可 checkpoint 的最小单元 | commit |
| Checkpoint | Task 完成时的状态快照（产出+上下文+校验） | commit 对象 |
| Fork/Join | 线内显式并行分支与收敛点 | merge |
| Module Tag | 任务影响的代码模块标签，用于冲突检测 | path（仅作标签） |
| Merge | 线验收通过后并入主线 | merge to main |
| Rebase | 线基于新基线重放任务 | rebase |

---

## 三、设计原则（硬约束）

1. **一 line 一目标**：无验收标准不得开线。
2. **线内默认线性**：并行必须显式 fork/join，禁止隐式并发。
3. **模块是标签不是划分**：Module 仅用于冲突检测与影响分析。
4. **线间异步，线内同步**：多线并行是架构主能力；单线内任务串行推进。
5. **每任务可 checkpoint**：任何 Task 完成必须产出可回滚快照。
6. **状态机显式**：任何状态变更必须由显式命令驱动，禁止后台静默推进。
7. **可审计**：所有变更留痕，`sele log` 可完整重放。

---

## 四、状态机设计

### 4.1 Line 状态机

```
draft ──open──> active ──┬──> blocked ──unblock──> active
                         ├──> done ──merge──> merged
                         └──> abandoned
```

| 状态 | 含义 | 允许操作 |
|---|---|---|
| draft | 已创建未激活 | 编辑 goal/acceptance/tasks |
| active | 推进中 | 执行 task、fork/join、block |
| blocked | 等待外部事件/审批 | 仅 unblock、abandon |
| done | 所有 task 完成且验收通过 | merge、reopen |
| merged | 已并入主线 | 只读 |
| abandoned | 放弃 | 只读，可 archive |

### 4.2 Task 状态机

```
pending ──start──> running ──┬──> done
                             ├──> failed ──retry──> running
                             └──> blocked ──unblock──> pending
```

| 状态 | 含义 |
|---|---|
| pending | 依赖未满足或未调度 |
| running | 执行中 |
| blocked | 等待审批/外部事件/冲突 |
| done | 完成且已 checkpoint |
| failed | 执行失败，需显式 retry 或 skip |

### 4.3 状态迁移约束

| 迁移 | 前置条件 |
|---|---|
| draft→active | 至少 1 个 task，goal/acceptance 非空 |
| active→done | 所有 task 为 done，验收检查全部通过 |
| done→merged | done 状态 + 无冲突 + 显式 --confirm |
| active→blocked | 存在 blocked 且阻塞性 task |
| 任意→abandoned | 显式操作，保留审计 |

所有状态变更记录到 `sele log`，带时间戳与操作者。

---

## 五、Line 规范

### 5.1 划分原则

**按功能/目标划分，不按代码模块划分。**

合规判定：

| 检查项 | 通过标准 |
|---|---|
| 目标性 | 一句话描述"完成后什么能力可用" |
| 可验收 | ≥1 条可判定验收标准 |
| 收敛性 | 有明确"这条线结束"的信号 |
| 独立性 | 与其他 active line 可独立推进 |
| 粒度 | 预估 task 数 3–15 |

反例：
- "重构 utils 目录" → 模块视角，应改为功能描述
- "改所有 TODO" → 无收敛信号
- "优化性能" → 不可验收

例外：纯基建工作作为 `chore` 类型 line 显式存在，仍需验收标准。

### 5.2 Line 元数据规范

```yaml
# .sele/lines/<line-id>/line.yaml
id: user-login                    # kebab-case，唯一
type: feature                     # feature|fix|chore|refactor|spike
goal: "用户可用邮箱密码登录并获得有效 session"
acceptance:
  - id: A1
    desc: "POST /login 正确凭证返回 200 + token"
    check: "pytest tests/test_login.py::test_success"
  - id: A2
    desc: "错误密码返回 401 且不泄露用户存在性"
    check: "pytest tests/test_login.py::test_wrong_pwd"
modules: [auth, session, db]      # 标签，非划分依据
base: main                        # fork 起点
owner: alice
created_at: 2026-10-29T10:00:00Z
status: active
```

字段约束：

| 字段 | 必填 | 约束 |
|---|---|---|
| id | 是 | `^[a-z][a-z0-9-]{2,40}$`，全局唯一 |
| type | 是 | 枚举值 |
| goal | 是 | 一句话，≤120 字，动词开头 |
| acceptance | 是 | ≥1 条，每条有 check |
| modules | 是 | ≥1 个标签 |
| base | 是 | 已存在的 line id 或 main |
| status | 是 | 状态机中的值 |

### 5.3 Task 规范

```yaml
# .sele/lines/<line-id>/tasks/<task-id>.yaml
id: T3
title: "实现 /login API"
depends: [T2]
group: null                       # 并行组 id，null 表示线性
modules: [auth, session]
prompt: |
  在 src/auth/ 下实现 POST /login ...
expected_output:
  - "src/auth/login.py"
  - "tests/test_login.py::test_success"
checkpoint:
  strategy: git-commit            # git-commit|snapshot|manual
  commit_msg: "feat(auth): implement /login"
status: done
created_at: ...
finished_at: ...
```

Task 约束：
1. 必须声明 depends（可为空，表示依赖前一个线性 task）
2. 必须声明 modules（用于冲突检测）
3. 完成必须产生 checkpoint
4. group 非空时同组并行，组内 depends 必须一致
5. 单线 task 数建议 ≤15

### 5.4 并行（Fork/Join）规范

结构：

```
线性轨道
  T1 ──> T2 ──[fork G1]──> T3a ─┐
                             T3b ─┴──[join G1]──> T4
```

| 规则 | 说明 |
|---|---|
| R1 | fork 与 join 必须成对，形成 G\<n\> 组 |
| R2 | 组内 task 的 depends 必须完全一致 |
| R3 | 组内 task 的 modules 不允许重叠（否则并行写冲突） |
| R4 | join 后 task 的 depends 必须包含组内全部 task |
| R5 | 禁止嵌套 fork（v0.1 限制） |
| R6 | 组内任一 task failed 未 retry，join 阻塞，line 进入 blocked |

### 5.5 冲突检测（`sele line check`）

| 检查 | 触发条件 | 处理 |
|---|---|---|
| 同组写冲突 | 并行组内 task modules 有交集 | 报错，要求改标签或串行化 |
| 跨线写冲突 | 两条 active line 的 modules 有交集且都在改 | 警告，提示 rebase 风险 |
| 依赖环 | task 依赖图有环 | 报错 |
| 孤儿 task | task 不被任何路径到达 | 报错 |
| 验收不可达 | task 图无法覆盖所有 acceptance | 警告 |

### 5.6 线间异步与协作

- 多条 line 可同时 active，互不阻塞
- 每条 line 有独立工作树（`.sele/lines/<id>/worktree/`）或独立 git 分支（`sele/<id>`）
- 线间共享基线 main，通过 rebase 同步

冲突处理流程：

```
line A 修改 auth 模块
line B 也修改 auth 模块
  → sele line check 报警
  → 后完成者需 sele line rebase <new-base>
  → rebase 时冲突 task 进入 blocked，需人工决策
```

### 5.7 Merge 规范

- Line 进入 done 后才能 merge
- Merge 前自动运行全部 acceptance 的 check
- Merge 冲突时 line 回到 active，冲突 task 标记 blocked
- Merge 后 line 状态 merged，工作树归档到 `.sele/archive/`

### 5.8 回滚与审计

| 操作 | 语义 | 命令 |
|---|---|---|
| Task 回滚 | 撤销某 task 的 checkpoint | `sele task revert <id>` |
| Line 回滚 | 回到某 checkpoint 时整线状态 | `sele line revert --to <task-id>` |
| 放弃线 | line→abandoned，保留审计 | `sele line abandon <id>` |
| 审计 | 完整重放所有状态变更 | `sele log [--line] [--task]` |

审计日志（append-only JSONL）：

```json
{"ts":"2026-10-29T10:01:00Z","actor":"alice","op":"line.open","line":"user-login"}
{"ts":"2026-10-29T10:05:00Z","actor":"agent","op":"task.start","line":"user-login","task":"T1"}
{"ts":"2026-10-29T10:20:00Z","actor":"agent","op":"task.checkpoint","line":"user-login","task":"T1","commit":"abc123"}
```

### 5.9 目录结构

```
.sele/
├── config.yaml                 # 全局配置
├── main/                       # 主线
├── lines/
│   └── user-login/
│       ├── line.yaml
│       ├── tasks/
│       │   ├── T1.yaml
│       │   └── T2.yaml
│       ├── worktree/           # 该线隔离工作区
│       └── checkpoints/        # task checkpoint 快照
├── archive/                    # merged/abandoned 归档
└── log.jsonl                   # 全局审计日志
```

---

## 六、CLI 命令规格

### 6.1 命令总览

```
sele init                              初始化 .sele/
sele line new <id> --goal --acceptance --modules --from --type
sele line list [--status]
sele line status <id>                  显示 DAG 与阻塞点
sele line check <id>                   冲突/依赖/验收检查
sele line switch <id>
sele line done <id>                    标记完成（跑 acceptance）
sele line merge <id> [--confirm]
sele line rebase <id> --onto <base>
sele line revert <id> --to <task-id>
sele line abandon <id>
sele line archive <id>

sele task add "<title>" [--depends] [--modules] [--group]
sele task fork --group <gid>
sele task join --group <gid>
sele task start <id>
sele task checkpoint <id> [--msg]
sele task done <id>
sele task fail <id> --reason
sele task retry <id>
sele task block <id> --reason
sele task unblock <id>
sele task revert <id>

sele status                            全局概览
sele log [--line] [--task] [--since]
```

### 6.2 关键命令详细规格

#### `sele line new`

```
sele line new <id> \
  --goal "<一句话目标>" \
  --acceptance "<desc>::<check>" [--acceptance ...] \
  --modules a,b,c \
  --from main \
  --type feature

校验：
  - id 格式与唯一性
  - goal 非空且 ≤120 字
  - acceptance ≥1
  - modules ≥1
  - from 存在
  - type 合法
输出：line id + 初始状态 draft
```

#### `sele line status`

```
输出：
Line: user-login [active]
Goal: 用户可用邮箱密码登录并获得有效 session
Modules: auth, session, db

Tasks:
  [done]    T1 设计 schema
  [done]    T2 实现 /login API
  [running] ├─ G1
  [done]    │  T3a 写单测
  [running] │  T3b 写文档
  [pending] T4 端到端验证 (depends: T3a, T3b)

Acceptance:
  [pass] A1 POST /login 返回 200 + token
  [todo] A2 错误密码返回 401

Blockers: none
```

#### `sele line check`

```
检查项（全部通过返回 0）：
  1. 依赖图无环
  2. 无孤儿 task
  3. 并行组内 modules 无交集
  4. join 依赖完整
  5. 跨线 modules 冲突警告
  6. acceptance 可达性
输出：问题列表 + 建议修复
```

#### `sele task fork/join`

```
sele task fork --group G1
  → 在当前线性位置插入 fork 点，后续 task 默认归属 G1
sele task add "写单测" --group G1 --modules tests
sele task add "写文档" --group G1 --modules docs
sele task join --group G1
  → 校验 R2/R3，插入 join 点，后续回到线性

校验失败提示：
  - 组内 modules 重叠 → "G1 内 T3a/T3b 都改 auth，请调整或串行化"
  - join 依赖不全 → "T4 未声明依赖 G1 全部 task"
```

---

## 七、Agent 集成约定

### 7.1 操作边界

| Agent 可以做 | Agent 不可以做 |
|---|---|
| 当前 active line 内 task start/checkpoint/done | 自行 line new（需人工） |
| 报告 task fail / task block | 自行 line merge |
| 请求 task retry | 跳过 line check |
| 读取 line status 决定下一步 | 隐式并行（必须显式 fork） |

### 7.2 约束注入

Agent 启动时注入当前 line 约束：

```
你是 sele line "user-login" 的执行 agent。
目标：<goal>
验收：<acceptance>
当前 task：<task-id> <title>
可改模块：<modules>
完成后必须执行：sele task checkpoint <id> && sele task done <id>
禁止：修改 line.yaml、创建新 line、跳过 checkpoint
```

### 7.3 失败与阻塞协议

- Task 失败 → `sele task fail <id> --reason "<原因>"`，line 不自动降级
- 需人工决策 → `sele task block <id> --reason "<问题>"`，line 进入 blocked
- 重试 → 人工或策略触发 `sele task retry <id>`

---

## 八、技术实现要点

### 8.1 存储

- 元数据：YAML 文件（line.yaml、tasks/*.yaml），便于人工审查与 diff
- 审计：append-only JSONL（log.jsonl），只追加不修改
- Checkpoint：默认 git-commit 策略，每 task 一个 commit（`sele/<line-id>` 分支）
- 工作树：每 line 独立 worktree，隔离并行写

### 8.2 校验引擎

- 结构化校验：JSON Schema 校验 YAML 字段合法性
- 图算法：DAG 构建 + 环检测（拓扑排序）+ 可达性分析
- 冲突检测：modules 集合交集运算，跨线扫描

### 8.3 并发控制

- 同 line 内：状态锁，禁止两个 task 同时 running（并行组除外）
- 跨 line：modules 级软锁，写入前检查交集，冲突告警
- 状态变更：乐观锁（版本号）+ 审计日志兜底

### 8.4 命令执行流程（以 `task done` 为例）

```
1. 读取 task 状态，校验 running
2. 执行 expected_output 存在性检查
3. 生成 checkpoint（git commit / snapshot）
4. 更新 task 状态 → done，写 finished_at
5. 检查依赖该 task 的下游是否可推进
6. 检查 line 是否可进入 done（全部 task done + acceptance 通过）
7. 写审计日志
8. 返回新状态
```

---

## 九、版本规划

### v0.1（首个可用版本）

- Line 状态机 + Task 状态机
- 线性 task + 单层 fork/join
- line check 六项核心检查
- Checkpoint（git-commit 策略）
- 审计日志
- CLI 命令全集（六、七章）

### v0.2+

- 嵌套 fork（diamond 嵌套）
- 跨线自动 rebase 策略
- 分布式多机执行
- Task 级 resource lock
- 图形化 DAG 视图
- Web UI 状态看板

---

## 十、设计决策汇总

| 决策点 | 结论 | 理由 |
|---|---|---|
| Line 划分依据 | 功能/目标 | 与 Git 分支意图一致，可验收可收敛 |
| 模块角色 | 标签 | 用于冲突检测与影响分析，不作划分 |
| 线内任务 | 默认严格串行 | 隐式状态依赖 + 共享工作区互斥 + 线性可回滚 |
| 并行能力 | 显式 fork/join | 兼顾独立性任务与可控性 |
| 异步落点 | 线间异步 + 线内线性 + 任务可阻塞 | 保留可回滚性，避免乱序失控 |
| Checkpoint 策略 | git-commit 默认 | 复用 Git 生态，天然可审计可回滚 |
| 状态变更驱动 | 显式命令 | 禁止后台静默推进，保证可预测 |

---

以上为 Sele 完整设计方案。如需进一步细化某一部分（如校验引擎伪代码、JSON Schema 定义、CLI 输出格式精确 spec），可单独展开。