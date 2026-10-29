# sele 与 git 的协作

> 适用版本：0.10.0。本文描述**当前实现**的行为，不是设想。  
> 一句话：git 负责存储与回滚，sele 负责目标、状态与验收；所有 git 操作走固定入口。

---

## 1. 职责分层

| 层 | 存什么 | 谁写 |
|---|---|---|
| git | 代码、分支、commit、worktree、merge/rebase/reset 历史 | 人 + sele（受控） |
| sele | line/task 状态、验收结果、会话指针、审计日志 | 只能由 `sele` 命令写 |

sele 不替代 git：它把「什么算完成、何时提交、提交后状态怎么记」编码成命令，agent 不需要自己决定 commit 时机与粒度。

---

## 2. 目录与跟踪策略

```
.sele/
├── config.json            tracked
├── log.jsonl              tracked（里程碑；.gitattributes 配 merge=union）
├── lines/<id>/            tracked（line.json + tasks/*.json）
├── archive/<id>/          tracked（line archive 移过来的）
└── runtime/               ignored（.sele/.gitignore）
    ├── state/<id>.json       task 状态、checkpoint sha、验收结果
    ├── sessions/<sid>.json   会话指针
    ├── events.jsonl          过程事件（start/checkpoint/fail）
    └── worktrees/<id>.json   worktree 指针
```

`sele init` 创建 `.sele/.gitignore`（内容 `runtime/`）与 `.sele/.gitattributes`（`log.jsonl merge=union`）。

**为什么拆两份**：

- `line.json` 与 `tasks/*.json` 入库：目标是团队要 review 的事实，随分支/提交走。
- task 运行态进 `runtime/`：高频写入，若入库会让工作树一直 dirty，破坏干净性判定与切分支。
- 代价：切到旧分支会看到旧的 line 状态；不同 worktree 的运行态各自独立（见 §8）。

---

## 3. 分支模型

```
main ──●──●──────────────────●──────────●
       init  plan            merge      record merge
              \             /
               ●──●──●──────●   sele/<line-id>
              checkpoint…  line done
```

- 一条 line ↔ 一个分支 `sele/<line-id>`。
- `line open` 创建分支；`line merge` 合并回 base（默认 `main`，`--onto` 可换）。
- `worktree add` 把同一分支检出到独立目录；指针在 runtime，不计入主工作树状态。
- base 分支上会先有 `plan` 提交（§4.1），保证 base 也拥有 `.sele/`。

---

## 4. 命令 → git 动作

### 4.1 提交时刻表

| sele 命令 | git 动作 | 提交 |
|---|---|---|
| `init` | 无（只建 `.sele/`） | 不自动 `git init` |
| `line open` | `git add -A` + commit `chore(sele): plan <id>`（仅当当前分支不以 `sele/` 开头）→ `git switch -c sele/<id>` | ✅ 元数据 |
| `task checkpoint` | `git add -A` + `git commit -m "<line>: <task> <title>"`（或 `--msg`） | ✅ **唯一的常规代码提交** |
| `task done` | 只跑 `git status` 做干净检查 | ❌ |
| `line done` | 默认 commit `.sele`（`chore(sele): line <id> done`）；`--no-commit` 关闭 | ✅ 元数据 |
| `line switch` | `git switch sele/<id>`（分支不存在则 `-c`） | ❌ |
| `line merge` | commit `mark <id> merged` → `git switch <base>` → `git merge --no-ff` → commit `record merge <id>` | ✅ 三处 |
| `line rebase` | `git rebase <onto>`；成功后 commit `chore(sele): rebase <id>` | ✅ 元数据 |
| `line revert` / `task revert` | `git reset --hard <checkpoint>` | ❌ 不提交 |
| `worktree add/remove` | `git worktree add/remove` | ❌ |
| `line archive` | 只做 `fs::rename`（`lines/` → `archive/`） | ❌ 不提交（见 §8.2） |
| `overview` | `git status --porcelain -- .sele`（提示未提交元数据） | ❌ |
| `lint`（legacy） | `git diff -- .design/DECISIONS.log`、`rev-parse` | ❌ |

提交信息一览：

| 时机 | 信息 |
|---|---|
| 开线（base 上） | `chore(sele): plan <line-id>` |
| checkpoint | `<line-id>: <task-id> <title>`（`--msg` 可覆盖） |
| 收线 | `chore(sele): line <line-id> done` |
| merge 前 | `chore(sele): mark <line-id> merged` |
| merge commit | `sele: merge <line-id>` |
| merge 后 | `chore(sele): record merge <line-id>` |
| merge 冲突回滚 | `chore(sele): unmark <line-id> merged` |
| rebase 成功 | `chore(sele): rebase <line-id>` |

> 实现位置：`line open` / `line switch` / `line done` / `line merge` / `line revert` / `line rebase` / `line archive` 在 `src/cli/line.cpp`；`task checkpoint` / `task done` / `task revert` 在 `src/cli/task.cpp`；上下文加载、干净性判定与 `save_*` 在 `src/cli/nextgen.cpp`；git 调用封装在 `src/core/gitrt.cpp`。

### 4.2 一次生命周期的提交序列

```
* chore(sele): record merge 20261029-fix-login     # merge 后在 main 记录 merge sha
*   sele: merge 20261029-fix-login                 # merge --no-ff
|\
| * chore(sele): line 20261029-fix-login done      # line done
| * 20261029-fix-login: T2 实现错误提示            # task checkpoint
| * 20261029-fix-login: T1 复现 401 分支           # task checkpoint
|/
* chore(sele): plan 20261029-fix-login             # line open（在 main 上）
* init
```

`plan` 提交在 **base** 上，不是 line 分支：否则切回 base 后 `.sele/` 消失，`sele` 在 base 上无法运行，merge 也读不到线状态。

---

## 5. 干净性判定

所有需要干净工作树的命令（`line open` / `line switch` / `task done` / `line merge` / `line rebase` / `line revert` / `task revert`）使用同一条判定：

```
git status --porcelain --untracked-files=no -- . ':(exclude).sele'
```

- 只检查**代码**，排除 `.sele/`。
- 原因：sele 自己会改被跟踪的元数据；若算进 dirty，`line open` 会在自己刚写完文件后失败。
- `.sele` 的未提交改动由下一次 `task checkpoint` 的 `git add -A` 顺带提交；`line done` 默认提交；`overview` 会提示还有未提交的元数据。

---

## 6. 冲突与回退的 git 语义

| 场景 | sele 行为 |
|---|---|
| merge 冲突 | 立即 `git merge --abort` → 切回 line 分支 → 状态回滚 `done` 并提交 `unmark` → 退 2，列出冲突文件 |
| rebase 冲突 | **不 abort**，停在冲突态；`line rebase --continue`（`core.editor=true`）/ `--abort` |
| `line revert --to T1` | `reset --hard <T1.checkpoint>`；保留 T1 done，T2 起重置 pending |
| `task revert T2` | `reset --hard <T1.checkpoint>`；T2 及之后重置 pending |
| `task revert T1`（根） | `reset --hard <base_commit>`；整条线回到 draft，需重新 `line open` |
| `line archive` | 不做 git 操作，目录移动留在工作树（§8.2） |

**为什么 merge 冲突 abort、rebase 冲突保留**：

- merge 是一次性动作，没有"继续"语义；保留半合并状态会让 line 状态机出现中间态。
- rebase 有 `--continue`/`--abort` 的原生语义，保留冲突态让人工解决后继续，符合 git 习惯。

---

## 7. 审计与可追溯

| 文件 | 内容 | 用途 |
|---|---|---|
| `log.jsonl`（tracked） | line 里程碑 + `task.done`（含 commit）+ `acceptance.*` | 跨机器可追溯；`fsck --fix` 据此重建 runtime |
| `runtime/events.jsonl`（ignored） | `task.start`/`checkpoint`/`fail`/`retry`/`block`/`line.switch` | 本机过程审计 |
| git 提交历史 | 代码 + `.sele` 元数据 | 最终事实来源，可 `git log`/`git bisect` |

`log.jsonl` 配 `merge=union`，多人并行时追加行可自动合并；损坏行由 `fsck` 报告行号。

---

## 8. 已知边界与风险

### 8.1 分支切换会让 `line.json` 状态回退

`.sele/lines/*.json` 随分支内容走。切到旧分支会看到旧的 line 状态；运行态在 `runtime/`（本机），所以 agent 的"当前任务"不会回退，但 line 的 `status` 会。

### 8.2 `line archive` 不自动提交

目录移动（`lines/<id>` → `archive/<id>`）是未提交的工作树改动。归档后该线只读，不会再有 checkpoint 顺带提交它。当前需要手动：

```bash
git add .sele && git commit -m "chore(sele): archive <id>"
```

这是实现缺口，后续可补"archive 后自动提交"或输出 `next` 提示。

### 8.3 `reset --hard` 会丢未提交的 `.sele` 改动

`line revert` / `task revert` 只保证代码干净（排除 `.sele`），`.sele` 的未提交元数据会被 reset 丢弃。这是"回到当时状态"的语义，但需要知道。

### 8.4 rebase 改写 line 分支历史

只允许未合并的线；merged/abandoned 拒绝。rebase 后 `base_commit` 更新为 base 的 HEAD。冲突解决后 `--continue` 用 `-c core.editor=true`，不会卡在编辑器。

### 8.5 merge 冲突会在 line 分支留下两条 chore 提交

`mark` / `unmark` 是审计痕迹，不影响代码；重试 merge 会再产生一对。

### 8.6 worktree 的运行态不同步

不同 worktree 的 `.sele/runtime/` 各自独立（会话指针、task 状态、events）；`.sele/lines/` 随分支/提交同步。多 agent 并行时这是预期隔离，但不要指望 A 工作树的 `task start` 在 B 工作树可见。

### 8.7 不推送、不拉取

sele 不 fetch/push。远端同步仍由人决定；`line merge` 只操作本地分支。

### 8.8 `.sele` 与代码同分支的取舍

好处：元数据随线走，review 时能看到"这条线改了什么计划"。
代价：状态随 checkout 回退；因此把高频状态放进 runtime。

---

## 9. 与 legacy（`.design` / `.agent`）的差异

| 维度 | `.sele`（当前） | `.design`（legacy） |
|---|---|---|
| 提交 | `task checkpoint` 自动提交代码 + 元数据 | 不提交（除 `lint` 读 `git diff`） |
| 分支 | 一线一分支 `sele/<id>` | 无 |
| 状态 | 显式状态机 + JSON | STATE.md 四行文本 |
| 审计 | `log.jsonl` + events | `DECISIONS.log`（人读散文） |
| 回滚 | `reset --hard` / `merge` / `rebase` | 无 |

旧命令（`new`/`append-decision`/`status`/`lint`/`close`）在 `.sele` 项目里退 4；两者同时存在时旧命令仍可用（迁移期）。

---

## 10. 设计取舍总结

1. **checkpoint 是唯一的常规代码提交**：单一入口，agent 不需要判断时机；提交粒度 = task。
2. **元数据入库、运行态忽略**：可 review + 工作树可判定干净。
3. **干净判定排除 `.sele`**：避免 sele 自己的写入阻塞自己的前置检查。
4. **merge 先标记后合并**：保证 `merged` 状态随合并进入 base；冲突则回滚状态，不留半合并。
5. **rebase 保留冲突态**：复用 git 的 continue/abort 语义。
6. **base 上先提交 plan**：保证 base 拥有 `.sele`，merge 与后续操作可用。
7. **不推送**：把远端策略留给人和 CI。
