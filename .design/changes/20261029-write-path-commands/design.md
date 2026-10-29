# 20261029-write-path-commands

把"结构类文档字段"的写入从 agent 手改文件收归 sele 命令：新增 `task`、`state`、`handover` 三组子命令，抽出 `core/state` 与 `core/fs`。

边界：本变更只做**写路径**。不改 `close` 折叠算法、不改 `lint` 检查项、不加 `note` / `index` 命令、不做并发强保证、不升版本号——这些留给后续变更。散文类内容（SPEC 正文、design.md 论证、决策文本）仍然自由书写，工具只独占结构。

> 折叠提醒：下面各 `##` 小节写的是**折叠进 SPEC.md 后的目标现状全文**，不是增量 diff。`close` 命中同名小节时整节替换，未提及的小节保持 SPEC 原文。

## 命令表面

全局选项：`--root <path>`（默认当前目录，向上查找 `.design/`）、`--output text|json`（默认 text）、`--version`（单行摘要）。

| 子命令 | 参数 | 作用 |
|---|---|---|
| `init` | `[directory]` / `--root` | 建 `.design/` 与 `.agent/` 及模板文件；已存在则退出 2 |
| `new` | `--slug`（必填） | 生成 id，建双侧目录与 design.md / tasks.md / handover.md，把 STATE 指向新 id |
| `append-decision` | `--context` `--decision` `--consequences`（必填）、`--change-id` | 向 DECISIONS.log 追加 `## [n]` 记录 |
| `status` | — | 读 STATE（指针与 Phase / Blocked / Next）、tasks.md 勾选数、该 id 的决策条数 |
| `lint` | `--fix` | 四项机械检查 |
| `close` | `<id>`（必填）、`--dry-run` | 把 design.md 折叠进 SPEC.md |
| `version` | — | 版本与依赖版本；不挂 `--root` |
| `task list` | `[<id>]`、`--pending` | 列出 tasks.md 的勾选项，序号从 1 起（含已完成项） |
| `task add` | `[<id>]`、`--text`（必填） | 在末尾追加一条 `- [ ]` 项 |
| `task done` / `task undone` | `[<id>]`、`<n>...`（必填） | 按序号勾选 / 取消勾选 |
| `state show` | — | 输出 STATE 四个字段（缺失为空串） |
| `state set` | `--phase` / `--blocked` / `--next` | 只改写给定字段，其余行原样保留 |
| `handover write` | `[<id>]`、`--done` / `--left` / `--next` / `--note`、`--from-file`、`--dry-run` | 按三段模板覆写 handover.md |
| `handover show` | `[<id>]` | 原样输出 handover.md |

`init` 可在空目录运行；其余命令要求 `.design/` 与 `.agent/` 存在，缺失即报错退出（`new` 另做显式检查并提示 `sele init`）。`version` 与 `--version` 不依赖目录，任意位置可运行。

### 变更 id 的省略规则

`task`、`handover` 的 `<id>` 可省略，此时取 `.agent/STATE.md` 的 `Active Change:`；该值为 `无` 或缺失时退出 2，并提示 `sele state show`。`close` 仍要求显式 `<id>`，保持既有行为。

### task 的序号语义与幂等

- 序号是 `tasks.md` 中所有 checkbox 项（`- [ ]` / `- [x]` / `- [X]`）的**全文顺序号**，从 1 起，标题与空行不占号；`--pending` 只影响输出，不影响编号。
- `task done` 对已勾选项、`task undone` 对未勾选项是**幂等**操作：退出 0，`changed` 为 `false`，不重写文件。
- 序号越界退出 2；`--text` 为空退出 1。
- `tasks.md` 不存在时，`task add` 按模板创建后再追加；`task done/undone/list` 退出 2。

### state 的写入边界

- `state set` 只允许改 `Phase` / `Blocked` / `Next`；`Active Change` 只能由 `new` 与 `close` 改写，命令面不提供入口。
- 未指定的字段保持原行不动；STATE 中未知的行、注释、空行原样保留（按行编辑，不整文重写）。
- 值为空字符串时写入 `无`；三个字段都没有给定则退出 1。
- `state set` 幂等：目标值与现值一致时 `changed` 为 `false`，不重写文件。

### handover 的两种写入模式

- 结构化模式（`--done` / `--left` / `--next` / `--note`）：按固定三段模板渲染，未提供的段落写占位注释。
- 全文模式（`--from-file -` 读 stdin 或 `--from-file <path>`）：整文替换，内容原样落盘。
- 两种模式同时使用退出 1；两种模式都没有内容退出 1。
- 渲染结果与现有文件逐字节相同则 `changed` 为 `false`，不重写。

### 长文本入参

`handover write` 支持 `--from-file <path|->`（`-` 为 stdin）整文写入，与三段结构化参数互斥，避免 shell 引号与转义损耗。`task` / `state` 承载的是短字段，仍走命令行参数。`append-decision` 的字段级长文本入参不在本变更范围内，留待后续（见已知限制）。

## 输出

每个子命令先构造一份 JSON，再由同一份数据渲染 text，经 `Output::print_both(text, json)` 按格式择一输出。text 形态：

- `status`：四行摘要，无活跃变更时首行为 `Active change: none`；STATE.md 带 `Phase:` / `Blocked:` / `Next:` 时各追加一行，JSON 恒有 `phase` / `blocked` / `next`（缺失为空串）。
- `init` / `new`：首行摘要加缩进明细（design / work / agent 路径）。
- `append-decision`：`Appended decision #<n> -> <path>` 单行。
- `lint`：首行 `Lint passed` / `Lint failed`，随后每项 `[ok]` / `[fail]` / `[skip]`。
- `close`：预览为 `Close preview for <id> ...` 加折叠计划，实际关闭为 `Closed <id> (spec folded, directories kept)`。
- `version`：`sele <版本>` 加 `cli11` / `nlohmann_json` 两行。
- `task list`：每行 `<n>. [ ] <text>` / `<n>. [x] <text>`；`task add` 为 `Added task #<n>`；`task done` / `task undone` 为 `task #<n> done (done/total)`。
- `state show`：四行 `Active Change: ...` / `Phase: ...` / `Blocked: ...` / `Next: ...`，缺失值显示为 `-`。
- `state set`：`State updated: <以逗号分隔的字段名>`；无变化时为 `State unchanged`。
- `handover write`：`Handover written: <path>`（`--dry-run` 时前缀改为 `Handover preview: `）；`handover show` 原样输出文件内容。

```text
Active change: 20261020-my-feature
Tasks: 2/5 done
Decisions: 3
Consistent: yes
```

`task` / `state` / `handover` 的写操作 JSON 都含三个通用字段：`command`（如 `task.done`）、`changed`（bool，是否真的写入）、`path`（受影响文件）；既有命令的输出不变。错误输出保持 `{"error","suggestion"}`，并新增可选 `next` 数组（可直接执行的命令，供 agent 自行纠正），仅在能给出明确后续动作时出现，缺失时不产生该字段。

```json
{"error":"No active change","suggestion":"Run sele state show","next":["sele state show","sele new --slug my-feature"]}
```

退出码：0 成功，1 用法错误，2 状态冲突，3 lint 未通过，4 环境错误。

## 变更模型

- id 为 `<YYYYMMDD>-<slug>`，日期取 UTC，slug 限 `[a-z0-9-]`。
- 一次变更同时拥有 `.design/changes/<id>/design.md`、`.agent/work/<id>/tasks.md` 与 `.agent/work/<id>/handover.md`；前两者必须同时存在，由 `lint` 与 `close` 校验，`handover.md` 由 `new` 生成。
- `.agent/STATE.md` 的 `Active Change:` 是唯一指针，`无` 表示无活跃变更；可附 `Phase:` / `Blocked:` / `Next:` 行描述当前阶段、阻塞与去向。指针只由 `new` 与 `close` 改写，另三行只由 `state set` 改写；任何命令都不删除未知行。
- `tasks.md` 由 `task` 命令编辑：`- [ ]` 与 `- [x]` / `- [X]` 是唯一认可的勾选形态，序号按全文顺序计算。
- `handover.md` 由 `new` 按模板生成、由 `handover write` 整体覆写，只保留最新一份；不自动追加历史。
- 结构类字段（`tasks.md` 勾选、STATE 四行、handover 三段、DECISIONS.log 条目）只经 `sele` 渲染写入；散文与论证仍由人或 agent 自由书写。
- 关闭后两侧目录原地保留，历史交给 git；工具不删除、不移动、不归档。

## 约束

- C++20，CMake ≥ 3.20；依赖 CLI11 2.4.2 与 nlohmann/json 3.11.2，由 FetchContent 拉取，均为 header-only。
- 对外契约稳定：JSON 字段名、退出码、参数、目录布局。新增字段不算破坏；改取值算破坏。
- 成功信息走 stdout，错误走 stderr；text 与 json 由同一份数据渲染，内容一致。
- 警告即错误：GCC/Clang 用 `-Wall -Wextra -Wpedantic -Werror`，MSVC 用 `/W4 /WX`。当前可用构建链是 MinGW Makefiles + g++；MSVC 因 `gmtime` 的 C4996 报错尚不可用。
- `.design/SPEC.md` 不写过程性词汇，词表见 `src/cli/lint.cpp`。
- 不设系统默认安装点，安装前缀始终由 `--prefix` 指定。
- 结构写入单点：所有受管文件的落盘都经 `core::fs::write_atomic`（临时文件 + 改名），所有 STATE 读写都经 `core::state`，不在命令里内联第二套实现。
- 写命令幂等：目标状态已达成时返回 0 且 `changed` 为 `false`，不重写文件，避免无意义 diff 与 mtime 变动。
- 整文写入类命令（`handover write`）提供 `--from-file <path|->`，避免 shell 转义；字段级长文本（`append-decision`）仍走命令行参数，属已知限制。
- 并发保护是尽力而为的：不引入锁文件，不做原子比较交换；多进程同时写入同一文件时以后写者为准，属已知限制。

## 构建与测试

`cmake -B build -S .`（首次经 FetchContent 拉取依赖，需联网）、`cmake --build build`；产物在嵌套目录 `build/build/sele`。`cmake --install build --prefix <dir>` 装到 `<dir>/bin`。

`ctest --output-on-failure` 运行 32 个用例，工作目录为仓库根；Windows 脚本参数为 `-SeleBin`，Linux 脚本需要 `jq`。用例从 `new` 的 JSON 读取 id，不硬编码日期。

新增源文件：`src/cli/task.cpp`、`src/cli/state.cpp`、`src/cli/handover.cpp`、`src/core/state.cpp`、`src/core/fs.cpp`（新增 `task` / `state` / `handover` 三组子命令后，注册点仍集中在 `src/cli/commands.hpp` 与 `src/main.cpp`，源文件列表在 `CMakeLists.txt` 手工维护）。
