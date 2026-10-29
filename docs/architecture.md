# sele 架构梳理

> 本文描述代码库的**现状**（0.2.2 的 legacy 层：`.design` / `.agent` 与 7 个旧命令）。0.3.0 起新增 `.sele` 线任务体系（`line` / `task` / `acceptance` / `overview` / `log` / `fsck`），以 `docs/sele-v1-spec.md` 为准；本文的旧命令与 `core/` 基础模块仍然有效。事实来源：`CMakeLists.txt`、`src/`、`tests/`、`.design/SPEC.md`、`.agent/notes.md`。  
> 本文不修改任何设计契约；与 `.design/SPEC.md` 冲突处，以 SPEC 为准并应在后续变更中修正。  

---

## 1. 定位与边界

`sele` 是一个单可执行文件的 C++20 CLI，管理项目下的两个目录：

| 目录 | 角色 | 内容性质 |
|---|---|---|
| `.design/` | 设计真相 | 长期有效：`SPEC.md`（现状）、`DECISIONS.log`（只追加）、`changes/<id>/design.md`（单次增量） |
| `.agent/` | 执行工作区 | 过程性：`CONTRACT.md`（护栏）、`STATE.md`（指针）、`work/<id>/tasks.md`（进度） |

它只做五件事，对应一个闭环：**创建变更 → 追加决策 → 查询状态 → 规则检查 → 折叠回 SPEC**。

明确不做（当前版本）：MCP server、worktree 编排、自动 git 提交、目录删除/归档、历史回滚。`close` 之后两侧目录原地保留，历史交给 git。

---

## 2. 分层与依赖方向

```
                 ┌─────────────────────────────────────────┐
                 │ src/main.cpp                            │
                 │ 组装 CLI11 app / 全局选项 / 异常→退出码 │
                 └───────────────┬─────────────────────────┘
                                 │ register_*(app, global_opts, exit_code)
        ┌────────────────────────▼─────────────────────────┐
        │ src/cli/  子命令表面层                           │
        │ 参数声明 + footer 示例 + callback（读→改→输出）  │
        └───────┬──────────────────────────────┬───────────┘
                │                              │
   ┌────────────▼────────────┐     ┌───────────▼───────────┐
   │ src/core/  领域逻辑     │     │ src/util/  横切关注点 │
   │ root / id / template /  │     │ output（text/json）   │
   │ git / version           │     │ exit_codes（常量）    │
   └────────────┬────────────┘     └───────────┬───────────┘
                │                              │
   ┌────────────▼────────────┐     ┌───────────▼───────────┐
   │ src/thirdparty/ 依赖访问│     │ nlohmann/json         │
   │ CLI11 / json 版本符号   │     └───────────────────────┘
   └─────────────────────────┘
```

依赖规则（实际成立）：

- `cli/` 可以依赖 `core/`、`util/`、`thirdparty/`；`core/` 只能依赖标准库、`thirdparty/` 与生成头 `<sele_version.hpp>`。
- `util/output` 依赖 `nlohmann/json`；`util/exit_codes` 零依赖。
- **不存在反向依赖**：`core/` 不知道 CLI、不打印任何东西；输出与退出码只在 `cli/` 与 `main.cpp` 出现。

---

## 3. 模块清单

### 3.1 入口与共享声明

| 文件 | 职责 | 要点 |
|---|---|---|
| `src/main.cpp` | 构造 `CLI::App`、注册全局选项与 7 个子命令、统一异常映射 | `require_subcommand(1)`；`--version` 走 `set_version_flag`；顶层 catch 把 `CLI::ParseError` 映射为 1、`std::exception` 映射为 4 |
| `src/cli/commands.hpp` | `GlobalOptions` 结构 + `register_*` 声明 + `add_global_opts` / `add_output_opt` | 头文件**故意不叫 `cli.hpp`**，否则在大小写不敏感文件系统上会劫持 `#include <CLI/CLI.hpp>`（见 `.agent/notes.md`） |
| `src/cli/commands.cpp` | 共享选项注册 | `--root`、`--output`（`CLI::CheckedTransformer` + `ignore_case`） |

### 3.2 子命令层（`src/cli/`）

| 文件 | 读 | 写 | 主要失败码 |
|---|---|---|---|
| `init.cpp` | 目标目录是否存在 | `.design/`、`.agent/` 及 6 个模板文件 | 2（已初始化）、4 |
| `new.cpp` | `.agent/STATE.md` | 双侧 `<id>` 目录、`design.md`、`tasks.md`、改写 STATE 指针 | 1（slug 非法）、2（ID 已存在）、4（未初始化） |
| `append_decision.cpp` | `.design/DECISIONS.log` 的 `## [` 计数 | 追加一条 `## [n]` 记录 | 1（缺参）、4 |
| `status.cpp` | STATE、`tasks.md`、`DECISIONS.log`、两侧目录 | 无（只读） | 4 |
| `lint.cpp` | changes/work 目录、`git diff`、`SPEC.md`、模板文件 | 仅 `--fix` 时补 `design.md` / `tasks.md` | 3、4 |
| `close.cpp` | `design.md`、`SPEC.md`、`STATE.md` | `SPEC.md`（条件写）、STATE 指针（条件复位） | 1（缺参）、2、4 |
| `version.cpp` | 编译期版本 + 依赖版本 | 无 | 0 |

### 3.3 领域逻辑（`src/core/`)

| 文件 | 接口 | 说明 |
|---|---|---|
| `root.cpp` | `is_initialized` / `find_design_root` / `find_agent_root` | 从起点**逐级向上**查找，`is_initialized` 要求两侧同时存在；`find_*` 只认单侧，找不到抛 `runtime_error`（调用方统一折成 4） |
| `id.cpp` | `validate_slug` / `generate_id` / `parse_id` / `validate_id` | slug = `^[a-z0-9-]+$`；id = `YYYYMMDD-slug`，日期取 **UTC**；`parse_id` 用 `^(\d{8})-(.+)$` |
| `template.cpp` | `render_*` | 所有文件内容的唯一生成点；`render_decision_entry` 生成四段式决策（UTC 时间戳） |
| `git.cpp` | `execute_git` / `execute_git_silent` / `is_git_repo` / `has_head` / `diff_decisions_log` | 通过 `popen` 调用系统 git；`silent` 版本吞掉 stderr 并忽略退出码 |
| `version.cpp` | `version_string` / `version_display` | 读生成头 `sele_version.hpp`；展示串拼上两个依赖版本 |

### 3.4 横切（`src/util/`)

| 文件 | 接口 | 说明 |
|---|---|---|
| `output.cpp` | `print_text` / `print_json` / `print_both` / `error` | `print_*` 只在匹配的格式下输出；`error` 一律写 stderr（JSON 时输出 `{"error","suggestion"}`） |
| `exit_codes.hpp` | `SUCCESS/USAGE_ERROR/STATE_CONFLICT/LINT_FAILED/ENV_ERROR` = 0..4 | 全工具唯一退出码定义 |

### 3.5 依赖访问层（`src/thirdparty/`)

| 文件 | 用途 |
|---|---|
| `deps.hpp` | 版本信息的唯一取用口：`SELE_DEPS_SHARED` 时调包装库符号，否则读编译期宏 |
| `deps_shared.hpp` | `__declspec(dllexport)` / `visibility` 宏与 `extern "C"` 声明 |
| `cli11_shared.cpp`、`nlohmann_json_shared.cpp` | 仅导出 `sele_cli11_version()`、`sele_nlohmann_json_version()` |

---

## 4. 一次调用的生命周期

1. `main()` 构造 `CLI::App`，`require_subcommand(1)`，挂 `--version`、`--root`、`--output`。
2. 依次调用 `cli::register_init/new/append_decision/status/lint/close/version`。**这一步只声明参数与回调，不执行任何业务**。
3. `app.parse(argc, argv)` 选中子命令后执行其 callback。
4. callback 内部统一形态：

```cpp
util::Output output(global_opts.output_format);
try {
    // 1) 定位 root（current_path 或 --root），find_design_root / find_agent_root
    // 2) 读文件 → 领域计算 → 写文件（严格处用临时文件 + rename）
    // 3) 先构造 nlohmann::json，再渲染 text，output.print_both(text, json)
    exit_code = exit_codes::SUCCESS;
} catch (const std::exception& e) {
    output.error(e.what(), "");
    exit_code = exit_codes::ENV_ERROR;
}
```

5. `main()` 返回 `exit_code`。

两个必须注意的实现约束（都在 `.agent/notes.md` 记录过）：

- **回调延迟执行**：callback 在 `register_*` 返回之后才运行，因此选项状态必须 `std::make_shared<Args>()` 堆分配并按值捕获；只有 `GlobalOptions&` 与 `int& exit_code` 按引用捕获。
- **选项在父级与子级各注册一次**：`main` 挂 `--root`/`--output`，子命令又经 `add_global_opts` 注册同名选项，两者绑定同一变量。当前测试均使用「子命令后置选项」或「全局前置选项」两种写法之一，混用行为未被测试覆盖。

---

## 5. 领域模型：ID、双目录与 STATE 指针

### 5.1 变更 ID

- 形式 `YYYYMMDD-slug`，例：`20261020-close-fold-into-spec`。
- 日期由 `generate_id` 取 UTC 当天，因此**跨时区/跨日运行会得到不同前缀**，测试因此从 `new` 的 JSON 读 id，而不是硬编码。
- `.design/changes/<id>/` 与 `.agent/work/<id>/` 用同一个 id 关联，两侧**必须同时存在**；这一不变式由 `lint`（`id_consistency`）与 `close`（前置检查）双重守护。

### 5.2 `init` 生成的目录（工具作为产品产出的布局）

```
.design/            .agent/
├── INDEX.md        ├── INDEX.md
├── SPEC.md         ├── CONTRACT.md
├── DECISIONS.log   └── STATE.md
└── changes/        └── work/
```

实际由 `src/core/template.cpp` 的 6 个 `render_*` 函数硬编码生成（`changes/`、`work/` 为空目录）。

### 5.3 仓库自身的目录（工具自举）

本仓库根同时是被管理的项目：`.design/` 含 `design-self.md`、`INDEX.md`、`SPEC.md`、`DECISIONS.log`、3 个历史变更；`.agent/` 含 `CONTRACT.md`、`INDEX.md`、`STATE.md`、`notes.md`、`sele0.2.2.md`、3 个 `work/<id>/tasks.md`。
它与 `init` 产物的差异见 §11.1。

### 5.4 STATE.md 契约

```
# Agent State

Active Change: <id> | 无
Phase: <当前阶段，可选>
Blocked: <阻塞项，可选>
Next: <下一条动作，可选>
```

- `Active Change:` 是**唯一指针**，`无` 表示空闲。
- `new` 与 `close` **只改写指针行**；`Phase/Blocked/Next` 原样保留。
- `status` 解析全部四个字段：text 按「存在才输出」追加行，JSON 恒有 `phase/blocked/next`（缺失为空串）。
- `close` 仅在指针**正是该 id** 时复位为 `无`，否则不动并在输出里说明指向谁（避免踩掉进行中的新变更）。
- 解析一律「整行前缀匹配 + 去空白」，没有真正的 Markdown 解析器。

---

## 6. 命令数据流

### 6.1 `init`

冲突判定极简：目标目录下 `.design/` 或 `.agent/` **任一存在**即退出 2，不做合并。目标目录优先级：位置参数 > `--root` > 当前目录。成功后写 6 个文件（无子目录模板）。

### 6.2 `new`

```
root = --root 或 cwd
→ is_initialized(root)?           否 → 4（提示 sele init）
→ validate_slug(slug)?            否 → 1
→ id = UTC 日期 + "-" + slug
→ 两侧目录任一已存在?              是 → 2
→ create_directories(两侧)
→ 写 design.md（render_design_template）
→ 写 tasks.md（render_tasks_template）
→ 改写 STATE.md 的 Active Change 行（无该行则追加）
→ 输出 id / design_path / work_path / state_updated=true
```

注意：`new` 不校验已有变更是否仍未关闭，也不检查 `design.md` 内容；`state_updated` 恒为 `true`；STATE 更新逻辑内联在 `new.cpp`，与 `close.cpp` 的 `set_active_change` 是两份独立实现。

### 6.3 `append-decision`

- 序号 = 文件中行首为 `## [` 的行数 + 1（不解析既有编号是否连续）。
- 文件不存在或为空时先写 `# Decisions` 头。
- 用 `std::ios::app` 追加，符合「只追加」不变式；**没有** `--edit` / `--delete` 入口。
- `--change-id` 可选，缺省写 `none`；不校验其格式，也不校验对应变更是否存在。

### 6.4 `status`

只读，永远退 0（除环境错误）。计算四组信息：

| 字段 | 来源 | 缺失时 |
|---|---|---|
| `active_id` | STATE 的 `Active Change:`（`无` → 空串） | 空串 |
| `tasks.{total,done}` | `work/<id>/tasks.md` 行首 `- [ ]` / `- [x]` | 0/0 |
| `decisions` | `DECISIONS.log` 中含 `Change: <id>` 的行数 | 0 |
| `consistent` | `changes/<id>` 与 `work/<id>` 是否同时存在 | 无活跃变更时恒 `true` |

### 6.5 `lint`

四项检查，全部在内存里累积为 `checks` 数组，最后统一渲染：

| 检查名 | 判据 | `--fix` |
|---|---|---|
| `id_consistency` | changes 与 work 目录名双向一一对应 | 不修 |
| `decisions_append_only` | `git diff -- .design/DECISIONS.log` 中出现以 `-` 开头且非 `---` 的行 | 不修；非 git 仓库或无 HEAD 记 `skip` |
| `spec_process_words` | `SPEC.md` 转小写后含 `todo`/`wip`/`待办`/`下一步`/`进行中` | 不修 |
| `dir_integrity` | 每个 change 有 `design.md`、每个 work 有 `tasks.md` | 按模板补建 |

`--fix` 只作用于第四项；补建后该项仍记 `passed: true`。全部通过退 0，否则退 3。

### 6.6 `close`

见 §7。

### 6.7 `version`

不依赖任何目录，只挂 `--output`。`version` 子命令与 `--version` 共用 `core::version_display()`。

---

## 7. `close` 折叠算法（本项目最复杂的部分）

### 7.1 解析

`parse_sections(content, &preamble, &sections)`：

1. 逐行扫描，行首为 `## ` 且**不在 fenced code 块内**时切分新小节；标题 `trim` 后保存。
2. 遇到围栏开头（≤3 空格 + ≥3 个 `` ` `` 或 `~`）时，向后找匹配的闭合围栏；**成对则整体原样拷贝**（其中的 `## ` 不是标题）；找不到闭合则按普通行处理，避免未闭合围栏吞掉后续小节。
3. 第一个 `## ` 之前的内容归入 preamble（如 `# Spec` 与其注释）。

### 7.2 内容判定

`has_content(body)`：剥离所有 `<!-- ... -->` 后若仍有非空白字符即为有内容。因此：

- 只有模板占位注释 → 无内容 → `skipped`；
- `<!-- 说明 --> 正文` → 有内容 → 参与折叠。

### 7.3 匹配与计划

```
folded = SPEC 现有小节（副本）
for 每个 design 小节 s:
    match = 在 folded 中第一个 titles_match(s.title, folded[i].title) 的下标
    titles_match = 两标题互为子串（任一 find 另一非空）
    if  !has_content(s.body) → skipped
    elif match >= 0          → replaced（folded[match].body = s.body）
    else                     → appended（folded.push_back(s)）
```

关键设计：**匹配跑在 `folded` 上而不是原始 SPEC 上**，所以同一份 `design.md` 里重复标题会合并到同一节，而不是留下重名节。

### 7.4 渲染与写入

- `render_document`：`trim_block(preamble)`（空则 `# Spec`）+ 每节 `\n\n## 标题\n正文`，末尾补一个换行。
- `trim_block` 对每行**只右剪空白**，保留行首缩进（否则嵌套列表与 fenced code 会被压平——已踩过的坑），并去掉块首尾空行。
- 写入前比较 `render(spec_sections)` 与 `render(folded)`：相同则**不写**（`spec_updated: false`），避免重复关闭产生无意义 diff 与行尾归一化。
- 需要写时走 `write_file`：写 `<path>.tmp` → `flush` → 检查流状态 → `fs::rename` 覆盖；失败抛异常 → 4。这是全项目唯一做原子写的路径。

### 7.5 指针复位

`state_updated = (STATE 的 Active Change == id)`；为真则写回 `Active Change: 无`。若 STATE 里存在 `Phase:` / `Blocked:` / `Next:`，text 追加 `Phase/Blocked/Next left as-is`——工具不猜这些字段在关闭后该写什么。

### 7.6 `--dry-run`

只做计划、不写文件。输出 `design_sections`、`spec_sections`、`suggested_mapping`（未匹配 → `""`）、`plan`（`section` + `action`），text 与 JSON 用同一套动作词 `replaced` / `appended` / `skipped`。

---

## 8. 输出契约

### 8.1 双形态同源

每个命令**先构造 `nlohmann::json`**，再用同一份数据渲染 text，交给 `Output::print_both(text, json)`。成功信息走 stdout，错误走 stderr。JSON 字段名为对外契约：新增字段是兼容的，改取值不是。

### 8.2 JSON 字段表

| 命令 | 顶层字段 |
|---|---|
| `init` | `initialized`, `root`, `design_path`, `agent_path` |
| `new` | `id`, `design_path`, `work_path`, `state_updated` |
| `append-decision` | `appended`, `index`, `path` |
| `status` | `active_id`, `phase`, `blocked`, `next`, `tasks{total,done}`, `decisions`, `consistent` |
| `lint` | `passed`, `checks[{name,passed,detail?}]`（`detail.deleted_lines` 或 `detail.skipped`） |
| `close --dry-run` | `dry_run`, `id`, `design_sections`, `spec_sections`, `suggested_mapping`, `plan[{section,action}]` |
| `close` | `dry_run`, `id`, `spec_path`, `spec_updated`, `folded[{section,action}]`, `state_updated` |
| `version` | `name`, `version`, `cli11`, `nlohmann_json` |

### 8.3 退出码

| 码 | 含义 | 产生位置 |
|---|---|---|
| 0 | 成功 | 各 callback 末尾 / help / version |
| 1 | 用法错误 | CLI11 `ParseError`；slug 非法 |
| 2 | 状态冲突 | `init` 已存在、`new` ID 已存在、`close` 目录或 `design.md` 缺失 |
| 3 | lint 未通过 | `lint` 有任一 check 失败 |
| 4 | 环境错误 | 未初始化、`find_*` 抛异常、IO 失败、其他 `std::exception` |

---

## 9. 构建系统

### 9.1 依赖与链接模式

| 模式 | 做法 | 产物 |
|---|---|---|
| 默认（`SELE_SHARED_DEPS=OFF`） | CLI11 / nlohmann_json 作为 header-only 编入 | 单个 `sele` |
| `=ON` | 额外构建 `cli11_shared`、`nlohmann_json_shared` 两个 SHARED 库，只导出 `sele_*_version()` 符号；可执行通过 `deps.hpp` 的条件分支调用 | `sele` + 2 个动态库，同目录 |

共享模式下编译期定义 `SELE_DEPS_SHARED`（对 `sele`）与 `SELE_DEP_BUILD`（对包装库），并设置 `BUILD_RPATH`/`INSTALL_RPATH = $ORIGIN`，Windows 靠同目录 DLL 查找。包装库只承载版本符号，用于建立并验证运行时动态依赖关系——模板代码本身仍随头文件进 exe。

### 9.2 版本单一来源

```
project(sele VERSION 0.2.2)
        │ configure_file(@ONLY)
        ▼
build/generated/sele_version.hpp   →  #define SELE_VERSION_STRING "0.2.2"
```

源码中没有任何版本字面量：`core::version_string()` 读生成宏，`version_display()` 拼接 `deps::*` 的版本。

### 9.3 目录与目标

- 所有产物置于 `${CMAKE_BINARY_DIR}/build`（`RUNTIME_OUTPUT_DIRECTORY`），因此二进制在**嵌套路径** `build/build/sele`。
- `install(TARGETS sele RUNTIME DESTINATION bin)`；共享模式下包装库的 `RUNTIME` 与 `LIBRARY` 也装进同一 `bin/`。前缀只由 `--prefix` 给定，不设系统默认安装点。
- 警告即错误：GCC/Clang `-Wall -Wextra -Wpedantic -Werror`，MSVC `/W4 /WX`。当前可用链为 MinGW Makefiles + g++；MSVC 因 `gmtime` 的 C4996 被升级为错误而不可用。
- `enable_testing()` + `add_test(NAME cli ...)`：Windows 用 `powershell -File tests/cli_tests.ps1 -SeleBin <bin>`，其他平台用 `bash tests/cli_tests.sh <bin>`，工作目录为仓库根。

### 9.4 一处容易忽略的构建细节

除 `cmake_minimum_required(3.20)` 外还显式设置 `CMAKE_POLICY_VERSION_MINIMUM 3.5`，让较新版本的 CMake 接受本工程的最低版本声明。首次配置需要联网拉取两个依赖。

---

## 10. 测试体系

| 项目 | 现状 |
|---|---|
| 用例数 | 20（`ctest` 一个 `cli` 测试项，内部串行跑 20 个函数） |
| 脚本 | `tests/cli_tests.sh`（GNU 工具 + `jq`）、`tests/cli_tests.ps1`（PowerShell，参数 `-SeleBin`），两份**同构但独立维护** |
| 夹具 | `setup_project()` 用 `mktemp -d` + 手工 `mkdir` 造出 `.design/`、`.agent/`（**绕过 `sele init`**，因此 `new` 的初始化检查能通过） |
| 隔离性 | 每个用例自建临时根并在结束时删除；id 从 `new` 的 JSON 读取，不硬编码日期 |
| 覆盖重点 | happy path 与失败码、默认 text 输出非静默（6 个命令）、`close` 折叠的四个边界（重复标题合并、行内注释正文、fenced code 存活、未匹配不编造映射）、STATE 指针与三字段的读写、版本一致性 |

断言风格是「grep 首行 + jq 校验字段 + 检查文件内容」，没有单元测试框架；`core/` 的函数只通过 CLI 端到端被覆盖。

---

## 11. 架构观察（只记录，未修改）

以下为梳理时发现的**现状与契约/文档的偏差**或重复点，供后续变更决策参考。

### 11.1 `init` 模板与 `.init-template/` 是两套

- `sele init` 的内容全部来自 `src/core/template.cpp` 的硬编码字符串；`.init-template/` 目录（含 `design-self.md`、`阶段计划.md`、`notes.md`、`sele0.2.2.md`）**没有任何代码引用**。
- 结果：`init` 生成的 `.design/INDEX.md` 是两行占位，而 `.init-template/.design/INDEX.md` 是完整路由表；`init` 不产出 `design-self.md`、`阶段计划.md`、`notes.md`、`prompts/`、`handover.md`。
- 两套模板会各自漂移，改一处不影响另一处。

### 11.2 `core::git` 的 `repo_path` 参数未生效

`execute_git(const std::filesystem::path&, args)` 与 `execute_git_silent` 都**忽略**第一个参数，直接以 `popen("git " + 拼接参数)` 在当前进程工作目录执行，既没有 `-C <path>`，也没有用 `argv` 数组。

- 后果：`lint` 的 `decisions_append_only` 实际比较的是**进程 cwd 所在仓库**，而非 `--root` 指向的仓库。在仓库根的父目录或子目录调用 `sele lint --root <other>` 时结论可能不对。
- 同时，这与 `.draft/mvp.md` 中「必须用参数数组执行、禁止拼接 shell 字符串」的要求相悖。
- 另：参数含空格时会被 shell 拆分；当前调用点参数都是固定字面量，尚未暴露。

### 11.3 STATE 读写逻辑有三份

| 位置 | 行为 |
|---|---|
| `new.cpp` | 内联查找 `Active Change:` 并替换/追加 |
| `close.cpp` | `active_change_value` / `has_state_field` / `set_active_change` |
| `status.cpp` | `state_field(content, key)` 通用读取 |

三处对「整行前缀匹配 + 去空白」的实现细节不同（`new` 用 `find`，`close` 用 `find`，`status` 逐行前缀匹配），`Phase/Blocked/Next` 的读取只存在于 `status`。抽出 `core/state.{hpp,cpp}` 可消除重复，但会改变文件布局，需在变更中决策。

### 11.4 写文件的两套严格度

- `close.cpp` 的 `write_file`：临时文件 + `flush` + 状态检查 + `rename`，失败抛异常 → 4。
- `init.cpp` 的匿名 `write_file` 与 `new.cpp` 的 `ofstream`：**不检查**流状态，磁盘满/权限不足可能静默成功。
- `append-decision` 用 `std::ios::app`，也未检查写入结果。

### 11.5 选项重复注册

`--root` / `--output` 在父 app 与每个子命令各注册一次，绑定同一个 `GlobalOptions` 变量。当前可用（取最后解析到的值），但属于依赖 CLI11 行为的隐含约定，测试未覆盖「`--root A new --root B`」这类混用。

### 11.6 文档漂移

- `.design/INDEX.md` 列出 `阶段计划.md`，但仓库根 `.design/` 下没有该文件（它只存在于 `.init-template/`）。
- `.agent/notes.md` 写「ctest 15 个用例」，实际脚本已注册 20 个（`.design/SPEC.md` 写的是 20）。
- `.agent/CONTRACT.md` 与实际仓库 `.init-template/.agent/CONTRACT.md` 的措辞不同（前者允许「经询问后」用脚本，后者一律禁止 Python），两份不会自动同步。

### 11.7 其他小点

- `lint` 的 `decisions_append_only` 只看工作区 vs HEAD；已提交的历史删改无法发现，`has_head` 为假时该项直接 `skip`。
- `new` 不校验 `Active Change` 是否仍指向未关闭的变更，可静默覆盖指针（`close` 侧有防踩踏逻辑，`new` 侧没有）。
- `close` 的标题匹配是双向子串，形如「输出」与「输出格式」会互相命中，取 SPEC 中**第一个**命中节；顺序敏感。
- `status` 的 `consistent` 只在有活跃 id 时计算；无活跃变更时恒为 `true`，不扫描全量目录（全量一致性属于 `lint` 的职责）。

---

## 12. 扩展指南

### 12.1 新增一个子命令（当前需要改 4 处）

1. 新建 `src/cli/<cmd>.cpp`，实现 `register_<cmd>(CLI::App&, GlobalOptions&, int& exit_code)`：
   - `add_subcommand` → `add_global_opts`（若需要 `--root`）→ 参数 → `footer("Example:\n  sele ...")`（MVP 硬性要求）
   - 选项状态用 `std::make_shared`；输出用 `print_both`；所有异常路径设 `exit_code`
2. 在 `src/cli/commands.hpp` 加声明。
3. 在 `src/main.cpp` 调用 `register_<cmd>`。
4. 在 `CMakeLists.txt` 的 `add_executable(sele ...)` 加源文件。

若命令需要新的领域逻辑，优先放 `src/core/`（纯逻辑、不打印），必要时新增源文件并同步 CMake —— 本项目没有 glob，**漏加源文件会在链接期报错**。

### 12.2 修改模板文本

只改 `src/core/template.cpp`。两条硬约束：

- `lint` 禁止 `SPEC.md` 出现 `todo`/`wip`/`待办`/`下一步`/`进行中`，若模板文本写进 `SPEC.md` 必须避开这些词。
- `render_design_template` 的小节标题应写成 **SPEC 中的概念名**，否则 `close` 只能追加新节，无法修订既有节。

### 12.3 改版本

只改 `CMakeLists.txt` 的 `project(sele VERSION x.y.z)` 并重新 configure；同时更新 `.agent/sele0.2.2.md` 与 `.init-template/.agent/sele0.2.2.md` 这类带版本号的文档名（当前无自动化）。

---

## 13. 建议的阅读顺序

1. `CMakeLists.txt` → 知道有哪些源文件与两种链接模式
2. `src/main.cpp` → 知道命令表面与错误映射
3. `src/cli/commands.hpp` / `commands.cpp` → 知道共享契约
4. 任选一个简单命令（`version.cpp` → `status.cpp`）→ 熟悉 callback 形态与输出双形态
5. `src/core/` 五个模块 → 领域规则
6. `src/cli/close.cpp` → 最复杂的数据流（解析 → 计划 → 渲染 → 原子写）
7. `tests/cli_tests.sh` → 行为契约的可执行版本
