# sele 人类使用指南

> 适用版本：0.11.1（`sele version` 可查）。  
> 面向人类阅读；agent 的操作手册见 `docs/sele-v0.10.0.md`，实现细节见 `docs/sele-v1-spec.md`。

---

## 1. sele 是什么

`sele` 是一个管理 `.sele/` 目录的命令行工具，用来组织「目标 → 任务 → 验收 → 合并」这条线：

- **line**：一个带验收标准的目标，对应一个 git 分支。
- **task**：线上的工作单元，完成时产生一次 checkpoint（git commit）。
- **acceptance**：线级验收项，由人或 agent 记账，工具不执行命令。
- **merge**：线完成后合并回 `main`，然后可以归档。

它的价值不在于“帮你写代码”，而在于**把什么时候提交、什么算完成、失败怎么回滚固定下来**，让 agent 和人遵循同一套规则。

从 0.11 起，`sele` **只支持 `.sele/`**；旧的 `.design/` + `.agent/` 体系不再被识别（目录可以保留，工具不会读写）。

---

## 2. 安装

```bash
cmake -B build -S .
cmake --build build
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
sele version
```

- 依赖（CLI11、nlohmann/json）由 CMake FetchContent 自动拉取，首次配置需要联网。
- 构建产物是 `build/build/sele`（注意嵌套的 `build/`）。
- 不想安装也可以直接用 `./build/build/sele`。
- 需要动态链接依赖时加 `-DSELE_SHARED_DEPS=ON`，包装库会装进同一个 `bin/`。

---

## 3. 五分钟快速开始

```bash
# 1) 项目要有 git 和至少一个提交
git init -b main
git add -A && git commit -m "init"

# 2) 初始化 .sele/
sele init

# 3) 建一条线（draft）
sele line new fix-login \
  --goal "登录失败时给出可操作错误" \
  --acceptance "错误密码返回 401::pytest tests/test_login.py::test_wrong_pwd" \
  --modules auth --type fix

# 4) 挂任务（默认串行）
sele task add "复现 401 分支" --modules auth
sele task add "实现错误提示" --modules auth

# 5) 开线：冻结验收、创建 sele/<id> 分支
sele line open <id>

# 6) 推进任务
sele task start T1
# ... 改代码 ...
sele task checkpoint T1 --msg "fix(auth): reproduce 401"
sele task done T1

sele task start T2
# ... 改代码 ...
sele task checkpoint T2
sele task done T2

# 7) 记账验收（工具不执行 check）
sele acceptance pass A1 --evidence "pytest 12 passed"

# 8) 收线、合并、归档
sele line done <id>
sele line merge <id> --confirm
sele line archive <id>
```

`<id>` 形如 `20261030-fix-login`（UTC 日期 + slug）。每步都可以加 `--output json`，方便脚本处理。

---

## 4. 核心概念

| 概念 | 含义 | 你要记住的事 |
|---|---|---|
| line | 一个目标 + 验收 + 分支 | 一次只做一件事；完成前不要开新线 |
| task | 线上的工作单元 | 一个 task 一次 checkpoint，完成即 `done` |
| checkpoint | 提交快照 | `task done` 之前必须有；唯一自动提交代码的地方 |
| acceptance | 线级验收 | 只记账，不跑命令；`line done` 前必须全 pass |
| frozen | 开线时冻结的 goal/acceptance 哈希 | 开线后改计划要 `line amend --reason` |
| session | 当前会话指向的线 | 多 agent 用 `--session` 隔离 |
| worktree | 一条线检出到独立目录 | 多线并行写代码时用 |
| epic | 长期目标容器，跨多条 line | 长期目标用；不建分支、不做验收 |
| merged / archived | 已合并 / 已归档 | 只读，不能再改 |

---

## 5. 生命周期与状态机

```
line:  draft ──open──> active ──done──> done ──merge──> merged
                         │  ↑                │
                         │  └── reopen ──────┘
                         ├── block ──> blocked ──unblock──> active
                         └── abandon ──> abandoned

task:  pending ──start──> running ──done──> done
                           ├── fail ──> failed ──retry──> running
                           └── block ──> blocked ──unblock──> running
```

关键前置：

- `line open`：draft、≥1 task、git 有提交、工作树干净。
- `task start`：所有 `depends` 已完成；组外没有 running 的 task。
- `task done`：有 checkpoint、工作树干净、line active。
- `line done`：所有 task done、所有 acceptance pass。
- `line merge`：line done、`--confirm`、工作树干净。

完整状态表见 `docs/sele-v1-spec.md` §5。

---

## 6. 日常工作流

### 6.1 多线切换

```bash
sele line list                 # 只看活跃线
sele line list --all           # 含已归档
sele line switch <id>          # 切分支 + 切会话指针
sele overview                  # 全局概览
```

### 6.2 多线并行写代码（worktree）

```bash
git switch main                          # 主工作树先离开该分支
sele worktree add <id> --path ../wt-1    # 独立目录检出 sele/<id>
cd ../wt-1
sele --root . line show <id>
sele worktree remove <id>                # 回到主工作树执行
```

每个 worktree 的 `.sele/runtime/` 是独立的；`.sele/lines/` 随分支/提交同步。

### 6.3 并行组（可选）

```bash
sele task add "单测" --modules auth --group G1 --depends T1
sele task add "文档" --modules docs --group G1 --depends T1
sele task add "联调" --modules core --depends T2 --depends T3
```

同组成员可同时 `running`；join 点的 `depends` 指向全部成员。`line check` 会校验组内依赖一致。

### 6.4 回退

```bash
sele line revert <id> --to T1   # 保留 T1 done，T2 起重置 pending
sele task revert T2             # 撤销 T2 本身
sele task revert T1             # 根任务：整条线回到 draft，需重新 open
```

revert 会 `git reset --hard`，只用于未合并的线。

### 6.5 同步与冲突

```bash
sele line rebase <id>           # 变基到 main
# 冲突时：git status 看文件 → 解决 → git add <file>
sele line rebase <id> --continue
sele line rebase <id> --abort   # 或放弃
```

merge 冲突时 sele 会自动 `git merge --abort` 并把状态退回 `done`，解决后重试。

### 6.6 Epic（长期目标）

Epic 是跨多条 line 的长期目标容器；它不建分支、不做验收，line 仍是执行与合并单元。

```bash
sele epic new auth-overhaul --goal "重构认证体系"
sele line new oauth --goal "..." --acceptance "..." --modules auth --epic auth-overhaul
sele epic show auth-overhaul
sele epic close auth-overhaul        # 所有子线结束后；--force 跳过
sele epic reopen auth-overhaul --reason "..."
sele epic abandon auth-overhaul --reason "..."
```

适用：跨阶段、长期存在、没有单一收敛点的目标。一次能收口的目标直接用 line。

---

## 7. 命令速查

### line

| 命令 | 作用 |
|---|---|
| `sele line new <slug> --goal ... --acceptance ... --modules ...` | 建线（draft） |
| `sele line list [--all] [--status <s>]` | 列出线 |
| `sele line show [<id>]` | 任务、验收、阻塞点 |
| `sele line check [<id>] [--strict]` | 任务图/冻结/模块检查 |
| `sele line open <id>` | 开线（冻结验收、建分支） |
| `sele line switch <id>` | 切当前会话 |
| `sele line block <id> --reason` / `unblock <id>` | 人工阻塞/恢复 |
| `sele line amend <id> --reason ...` | 改计划并重算冻结 |
| `sele line done [<id>] [--no-commit]` | 收线（默认提交元数据） |
| `sele line reopen <id> --reason` | 重新打开已完成的线 |
| `sele line abandon <id> --reason` | 放弃 |
| `sele line merge <id> --confirm [--onto main] [--delete-branch]` | 合并 |
| `sele line revert --to <task>` | 回到某 checkpoint |
| `sele line rebase [<id>] [--continue\|--abort]` | 变基 |
| `sele line archive <id>` | 归档 merged/abandoned 线 |

### task

| 命令 | 作用 |
|---|---|
| `sele task add "<title>" --modules ... [--group <g>] [--depends <t>]...` | 加任务 |
| `sele task list` / `start` / `checkpoint` / `done` | 推进 |
| `sele task fail --reason` / `retry` | 失败与重试 |
| `sele task block --reason` / `unblock` | 阻塞与恢复 |
| `sele task revert <task>` | 撤销任务 |

### epic

| 命令 | 作用 |
|---|---|
| `sele epic new <id> --goal "..."` | 建长期目标（active） |
| `sele epic list` / `show <id>` | 列表 / 详情 |
| `sele epic close <id> [--force]` / `reopen` / `abandon` | 关闭 / 重新打开 / 放弃 |
| `sele line new ... --epic <id>` / `sele line amend <id> --epic <id\|none>` | 关联 / 解除 |

### 验收与全局

| 命令 | 作用 |
|---|---|
| `sele acceptance pass\|fail <A-id> --evidence "..."` | 记账验收 |
| `sele overview` | 全局概览 |
| `sele log [--events] [--line <id>] [--limit <n>]` | 审计日志 |
| `sele fsck [--fix]` | 校验 `.sele` 一致性 |
| `sele worktree add/list/remove` | 工作树 |
| `sele init` | 初始化 `.sele/` |
| `sele version` | 版本 |

每个命令都支持 `--root <dir>`、`--output text|json`、`--session <id>`。

---

## 8. 配置

配置文件是 `.sele/config.json`，可以控制：

- 提交信息的语言（英文 / 中文）与 scope；
- 模块白名单；
- checkpoint 策略；
- 操作者身份来源。

详见 [`docs/sele-config.md`](sele-config.md)。

当前仓库的配置示例（中文 subject + 按模块 scope）：

```json
"commit": { "language": "zh", "scope": "module", "task_type": "auto" }
```

---

## 9. 输出与退出码

- 默认 text；`--output json` 输出单对象（列表命令为 `{items:[...]}`）。
- 成功写 stdout，错误写 stderr；错误 JSON 带 `error` / `suggestion` / `next`。
- `next` 里是可执行的修复命令，失败时优先照它执行。

| 退出码 | 含义 |
|---|---|
| 0 | 成功 |
| 1 | 用法错误（缺参数、非法 id/slug） |
| 2 | 状态冲突（非法迁移、line 不存在、工作树不干净、合并冲突） |
| 3 | `line check` / `fsck` 未通过 |
| 4 | 环境错误（无 `.sele`、无 git、IO/JSON 失败） |

---

## 10. 与 git 的关系

- 一条 line ↔ 一个分支 `sele/<id>`；`line open` 创建，`line merge` 合并回 `main`。
- `task checkpoint` 是唯一自动提交代码的命令；`line done` 默认提交 `.sele` 元数据。
- “工作树干净”只看代码，排除 `.sele/`。
- sele 不 push / pull；远端同步由人或 CI 决定。
- 详细提交时刻表与冲突语义见 [`docs/sele-git.md`](sele-git.md)。

---

## 11. 常见问题

**Q：`task done` 报 “has no checkpoint”？**
先 `sele task checkpoint <line> <task>`；checkpoint 是提交，不是标记。

**Q：报 “Working tree has uncommitted code changes”？**
先 checkpoint 或提交；`task done`、`line open`、`line switch`、`merge`、`rebase` 都要求代码干净。

**Q：`line open` 失败？**
检查：line 是 draft、至少一个 task、git 有提交、工作树干净。

**Q：merge 冲突了怎么办？**
sele 会 abort 并把线退回 `done`。在 `sele/<id>` 分支解决冲突、提交，再 `sele line merge <id> --confirm`。

**Q：rebase 冲突了怎么办？**
解决 → `git add` → `sele line rebase <id> --continue`；或 `--abort`。

**Q：提交信息为什么是中文/英文？**
由 `.sele/config.json` 的 `commit.language` 决定，`--msg` 可完全覆盖。见配置文档。

**Q：归档后还能改吗？**
不能。merged/abandoned 归档后只读；需要继续工作就新建 line。

**Q：`.design/.agent` 还能用吗？**
从 0.11 起不能。旧命令已移除，目录只是历史文件。

**Q：多个 agent 同时工作？**
给每个 agent 不同的 `--session`（或 `SELE_SESSION`），或各自用 `worktree`。

**Q：什么时候用 epic？**
目标跨多个阶段、长期存在（例如“重构认证体系”）时用 epic；能一次收口的目标直接用 line。epic 不建分支、不做验收，验收仍在子 line。

**Q：`sele init` 说已初始化？**
`.sele/` 已存在且初始化文件齐全时退 2；缺 `AGENTS.md` 或 `.sele/CONTRACT.md` 时会自动补齐（repair 模式）。

---

## 12. 文档地图

| 文档 | 用途 |
|---|---|
| `docs/sele-user-guide.md` | 本文：人类视角的安装、工作流、FAQ |
| `docs/sele-config.md` | 配置字段、环境变量、提交信息规则 |
| `docs/sele-v0.10.0.md` | 面向 agent 的操作手册（命令细节、JSON 契约） |
| `docs/feat-epic.md` | Epic 完整设计：数据模型、状态机、命令、校验、非目标 |
| `docs/sele-v1-spec.md` | 实现规格：状态机、数据模型、并发、审计 |
| `docs/sele-git.md` | 与 git 的协作：提交时刻表、干净判定、冲突 |
| `docs/README.md` | 文档索引 |
| `docs/architecture.md` | 0.2.2 时期的分层说明（legacy，部分已过时） |

日常只需要前两份；要改 sele 本身时再看 spec 与 git 文档。
