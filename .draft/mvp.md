# 任务：用 C++ 实现 design CLI 的 MVP

你要实现一个名为 `sele` 的 C++ 命令行工具，用于管理项目中的 `.design/` 和 `.agent/` 两个目录，支撑"变更创建 → 决策追加 → 状态查询 → 规则检查 → 折叠预览"的最小闭环。

本任务只做 MVP。不要实现真正的折叠（close 只做 --dry-run）、不要做 MCP server、不要做 worktree 编排。目标是产出一个可构建、可测试、Agent 能可靠调用的二进制。

## 1. 已有项目结构

项目根目录存在：

```
.design/
├── INDEX.md
├── SPEC.md
├── DECISIONS.log          # 可能不存在，首次 append 时创建
└── changes/               # 可能为空或不存在
.agent/
├── INDEX.md
├── CONTRACT.md
├── STATE.md
└── work/                  # 可能为空或不存在
```

`<id>` 格式统一为 `<YYYYMMDD>-<slug>`，例如 `20261018-temp-convert-cli`。`.design/changes/<id>/` 与 `.agent/work/<id>/` 通过同一个 `<id>` 关联。

## 2. 技术约束

- **语言标准**：C++20。
- **构建**：CMake ≥ 3.20，使用 `FetchContent` 拉取两个单头文件依赖，不引入包管理器：
  - `CLI11`（命令与参数解析）
  - `nlohmann/json`（JSON 输出与解析）
- **文件系统**：只用 `std::filesystem`。
- **Git 操作**：允许通过子进程调用系统 `git`，但**必须用参数数组形式执行**（`fork`/`execvp` 或 `posix_spawn`），**禁止**用 `system()` 拼接 shell 字符串。如果发现 libgit2 集成成本低于预期，可以用 libgit2 替代，但不要为此拖延 MVP。
- **平台**：优先 Linux/macOS；Windows 支持不作为 MVP 验收条件，但路径处理要用 `std::filesystem` 避免硬编码分隔符。
- **无第三方运行时依赖**：最终二进制静态链接 CLI11 和 nlohmann/json 后应可直接运行。

## 3. 面向 Agent 的 CLI 硬性设计底线

以下五条是验收条件，不是建议：

1. **非交互默认**：所有命令不得在缺少参数时弹出交互式提示。缺参数直接报错退出。
2. **结构化输出**：每个命令都支持 `--output json`，JSON 字段名一旦定下不得随意更改。
3. **`--help` 必须含具体示例**：每个子命令的 `--help` 输出中至少包含一条完整可执行的调用示例。
4. **错误信息可纠正**：错误输出必须说明"哪里错了 + 正确用法"，包含一条可直接复制的修正命令。
5. **退出码有语义**：全工具统一：
   - `0` = 成功
   - `1` = 用法错误（参数缺失、格式错误）
   - `2` = 状态冲突（如 ID 已存在、目标不存在）
   - `3` = 规则检查未通过（lint 失败）
   - `4` = 环境错误（目录不存在、git 不可用、权限不足）

## 4. 命令规格

所有命令都支持 `--root <path>`（默认当前目录，向上查找直到找到 `.design/`）和 `--output text|json`（默认 `text`）。

### 4.1 `design new --slug <name>`

创建一次变更的两侧目录。

行为：
1. 校验 `--slug` 只含 `[a-z0-9-]`，否则退出码 1。
2. 生成 ID = `<UTC 日期 YYYYMMDD>-<slug>`。
3. 若 `.design/changes/<id>/` 或 `.agent/work/<id>/` 已存在，退出码 2，不修改任何文件。
4. 创建 `.design/changes/<id>/design.md`，写入模板：
   ```markdown
   # <id>

   ## Context
   <!-- 为什么发起这次变更 -->

   ## 变更内容
   <!-- 改什么，只写设计决策，不写实现步骤 -->

   ## 影响面
   <!-- 影响哪些模块、接口、数据 -->

   ## 兼容性与迁移
   <!-- 是否需要迁移、是否破坏兼容 -->
   ```
5. 创建 `.agent/work/<id>/tasks.md`，写入模板：
   ```markdown
   # <id> 任务清单

   - [ ] 步骤 1
   - [ ] 步骤 2
   ```
6. 更新 `.agent/STATE.md`，将"活跃变更"字段设为该 ID，保留其他行。
7. 不自动执行 `git add` 或 `git commit`。

JSON 输出：
```json
{"id":"20261018-temp-convert-cli","design_path":".design/changes/.../design.md","work_path":".agent/work/.../tasks.md","state_updated":true}
```

### 4.2 `design append-decision --context <c> --decision <d> --consequences <e> [--change-id <id>]`

向 `.design/DECISIONS.log` 追加一条决策记录。

行为：
1. 三个参数缺一即退出码 1。
2. 若 `DECISIONS.log` 不存在则创建（含一行标题 `# Decisions`）。
3. 追加格式：
   ```
   ## [<序号>] <YYYY-MM-DD HH:MM:SS UTC>
   Change: <change-id 或 "none">
   Context: <c>
   Decision: <d>
   Consequences: <e>
   ```
   序号为文件中现有 `## [` 条目数 + 1。
4. **不提供任何编辑、删除、覆盖已有条目的 flag**。若检测到 `--edit` 或 `--delete` 参数，退出码 1 并说明"决策日志只追加"。
5. 追加采用"以 O_APPEND 打开 + 一次 write"方式，避免并发写交错。

JSON 输出：`{"appended":true,"index":3,"path":".design/DECISIONS.log"}`

### 4.3 `design status [--output json]`

只读命令，不修改任何文件，必须快。

行为：
1. 读取 `.agent/STATE.md`，提取活跃变更 ID（若为"无"则返回空活跃）。
2. 若活跃 ID 存在，解析 `.agent/work/<id>/tasks.md` 中 `- [ ]` 与 `- [x]` 的数量。
3. 统计 `.design/DECISIONS.log` 中 `Change: <id>` 匹配的条目数。
4. 如果 `.design/changes/<id>/` 与 `.agent/work/<id>/` 只有一侧存在，在输出中标记 `"consistent": false`，但退出码仍为 0（这是状态查询，不是检查）。

JSON 输出：
```json
{"active_id":"20261018-...","tasks":{"total":5,"done":2},"decisions":2,"consistent":true}
```

### 4.4 `design lint [--fix]`

机械规则检查。

必须检查以下四项，每项给出独立的 pass/fail 和说明：

1. **ID 双向一致性**：遍历 `.design/changes/*/` 与 `.agent/work/*/`，任何一侧缺失都算失败。
2. **DECISIONS.log 只追加**：用 `git diff --no-color -- .design/DECISIONS.log` 检查当前工作区相对 HEAD 是否存在删除行（以 `-` 开头且非 `---`）。若仓库无 git 或无 HEAD，跳过此项并标记 `"skipped": true`，不算失败。
3. **SPEC.md 过程性词汇**：检查 `.design/SPEC.md` 是否含 `TODO`、`WIP`、`待办`、`下一步`、`进行中`（大小写不敏感）。命中算失败。
4. **目录完整性**：每个 change 目录必须有 `design.md`；每个 work 目录必须有 `tasks.md`。

`--fix` 只允许自动修复第 4 项中"缺失模板文件"的情况（补建空模板），其余不自动修。

退出码：全部通过 = 0；任一项失败 = 3；检查过程本身出错 = 4。

JSON 输出：
```json
{"passed":false,"checks":[{"name":"id_consistency","passed":true},{"name":"decisions_append_only","passed":false,"detail":"删除行: 3"},{"name":"spec_process_words","passed":true},{"name":"dir_integrity","passed":true}]}
```

### 4.5 `design close <id> --dry-run`

**本任务只实现 `--dry-run`，不带 `--dry-run` 时退出码 1 并提示"MVP 未实现真正的折叠"。**

行为：
1. 校验 `<id>` 两侧目录都存在，否则退出码 2。
2. 读取 `.design/changes/<id>/design.md` 全文。
3. 读取 `.design/SPEC.md`，按 `##` 标题拆成章节。
4. 输出一份预览：design.md 的四个小节分别列出，并提示人类需要把它们手工合并进 SPEC.md 的哪些章节（按标题关键词做简单匹配，匹配不到时列出所有章节让人类选择）。
5. 不修改任何文件。

JSON 输出：`{"dry_run":true,"id":"...","design_sections":[...],"spec_sections":[...],"suggested_mapping":{...}}`

## 5. 项目结构

```
CMakeLists.txt
src/
├── main.cpp                 # 注册子命令，解析全局 flag
├── cli/
│   ├── new.cpp
│   ├── append_decision.cpp
│   ├── status.cpp
│   ├── lint.cpp
│   └── close.cpp
├── core/
│   ├── root.cpp             # 向上查找 .design/，解析 --root
│   ├── id.cpp               # 生成、校验、解析 <id>
│   ├── template.cpp         # 模板渲染
│   └── git.cpp              # git 子进程封装（argv 数组）
└── util/
    ├── output.cpp           # text/json 双输出封装
    └── exit_codes.hpp       # 统一退出码常量
tests/
├── cli_tests.sh             # 每个命令 happy path + 失败 path
└── fixtures/                # 临时项目根，用于测试
```

CMake 目标名 `design`，输出到 `build/design`。

## 6. 测试要求

在 `tests/cli_tests.sh` 中实现至少以下用例，用临时目录（`mktemp -d`）构造干净项目根：

1. `new` 成功：两侧目录生成、STATE.md 含 ID。
2. `new` 重复 ID：退出码 2，文件未被覆盖。
3. `new` 非法 slug：退出码 1。
4. `append-decision` 成功后：DECISIONS.log 含三段式记录，序号递增。
5. `append-decision` 缺参数：退出码 1。
6. `status --output json`：JSON 可被 `jq` 解析，`tasks.done` 与实际一致。
7. `lint` 在 ID 不一致时：退出码 3，JSON 中对应项 `passed:false`。
8. `lint` 在 SPEC.md 含 `TODO` 时：退出码 3。
9. `close --dry-run`：不修改任何文件（用 `git status --porcelain` 或文件 mtime 校验）。
10. `close` 不带 `--dry-run`：退出码 1。

测试脚本作为 CTest 的一个用例注册：`add_test(NAME cli COMMAND bash tests/cli_tests.sh $<TARGET_FILE:design>)`。

## 7. 验收标准

在干净的临时项目根上，以下命令序列全部成功：

```bash
design --help
design new --help
design new --slug demo-change
design append-decision --context "选C++17" --decision "用CLI11+nlohmann" --consequences "无包管理器依赖" --change-id 20261018-demo-change
design status --output json
design lint
design close 20261018-demo-change --dry-run
```

要求：
- `build/design` 能在无网络环境下运行（依赖已静态编入）。
- 每个命令的 `--output json` 输出能被 `jq .` 解析。
- `ctest` 全部通过。
- 故意制造一次 ID 不一致，确认 `lint` 退出码为 3 且 JSON 指出具体是哪一对 ID。

## 8. 硬约束

- 不要自己实现参数解析器，用 CLI11。
- 不要用 `system()` 执行 git 命令。
- 不要在任何命令中默认进入交互模式。
- 不要为 MVP 之外的命令（真正的 close、MCP server、worktree）预留半成品实现。
- 不要把 `DECISIONS.log` 的写操作做成"读全文件 → 修改 → 写回"，只用追加。
- 每个子命令的 `--help` 中必须有至少一条完整调用示例。
- 如果某项验收无法达成，停止并报告，不要用注释掉测试或放宽断言的方式"通过"。

## 9. 最终交付物

1. 可构建的 CMake 项目，`build/design` 可运行；
2. 上述 5 个子命令按规格实现；
3. `tests/cli_tests.sh` 覆盖 10 个用例并通过 `ctest`；
4. 根目录 `README.md`，包含构建命令、每个命令的一行示例、退出码表；
5. 在 `.design/DECISIONS.log` 中追加至少两条本实现过程中的真实决策（例如依赖选型、输出格式约定）。

按以下顺序执行，每完成一步汇报一句再继续：
1. 搭 CMake + CLI11 + nlohmann/json，产出空壳 `design --help`；
2. 实现 `core/root`、`core/id`、`util/output`；
3. 实现 `new` 和 `status`；
4. 实现 `append-decision` 和 `lint`；
5. 实现 `close --dry-run`；
6. 写测试并跑通 `ctest`；