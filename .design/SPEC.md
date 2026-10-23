# Spec

<!-- 当前设计真相源：一个概念一节，只写现状。历史见 git，取舍理由见 DECISIONS.log。 -->

## 目标

`sele` 是面向 agent 的 CLI，管理两个目录：`.design/`（设计真相）与 `.agent/`（执行工作区）。它覆盖「创建变更 → 追加决策 → 查询状态 → 规则检查 → 折叠回 SPEC」的闭环，默认输出人可读的 text，同时提供 JSON 供程序消费。

## 约束

- C++20，CMake ≥ 3.20；依赖 CLI11 2.4.2 与 nlohmann/json 3.11.2，由 FetchContent 拉取，均为 header-only。
- 对外契约稳定：JSON 字段名、退出码、参数、目录布局。新增字段不算破坏；改取值算破坏。
- 成功信息走 stdout，错误走 stderr；text 与 json 由同一份数据渲染，内容一致。
- 警告即错误：GCC/Clang 用 `-Wall -Wextra -Wpedantic -Werror`，MSVC 用 `/W4 /WX`。当前可用构建链是 MinGW Makefiles + g++；MSVC 因 `gmtime` 的 C4996 报错尚不可用。
- `.design/SPEC.md` 不写过程性词汇，词表见 `src/cli/lint.cpp`。
- 不设系统默认安装点，安装前缀始终由 `--prefix` 指定。

## 命令表面

全局选项：`--root <path>`（默认当前目录，向上查找 `.design/`）、`--output text|json`（默认 text）、`--version`（单行摘要）。

| 子命令 | 参数 | 作用 |
|---|---|---|
| `init` | `[directory]` / `--root` | 建 `.design/` 与 `.agent/` 及模板文件；已存在则退出 2 |
| `new` | `--slug`（必填） | 生成 id，建双侧目录与 design.md / tasks.md，把 STATE 指向新 id |
| `append-decision` | `--context` `--decision` `--consequences`（必填）、`--change-id` | 向 DECISIONS.log 追加 `## [n]` 记录 |
| `status` | — | 读 STATE（指针与 Phase / Blocked / Next）、tasks.md 勾选数、该 id 的决策条数 |
| `lint` | `--fix` | 四项机械检查 |
| `close` | `<id>`（必填）、`--dry-run` | 把 design.md 折叠进 SPEC.md |
| `version` | — | 版本与依赖版本；不挂 `--root` |

`init` 可在空目录运行；其余命令要求 `.design/` 与 `.agent/` 存在，缺失即报错退出（`new` 另做显式检查并提示 `sele init`）。`version` 与 `--version` 不依赖目录，任意位置可运行。

## 输出

每个子命令先构造一份 JSON，再由同一份数据渲染 text，经 `Output::print_both(text, json)` 按格式择一输出。text 形态：

- `status`：四行摘要，无活跃变更时首行为 `Active change: none`；STATE.md 带 `Phase:` / `Blocked:` / `Next:` 时各追加一行，JSON 恒有 `phase` / `blocked` / `next`（缺失为空串）。
- `init` / `new`：首行摘要加缩进明细（design / work / agent 路径）。
- `append-decision`：`Appended decision #<n> -> <path>` 单行。
- `lint`：首行 `Lint passed` / `Lint failed`，随后每项 `[ok]` / `[fail]` / `[skip]`。
- `close`：预览为 `Close preview for <id> ...` 加折叠计划，实际关闭为 `Closed <id> (spec folded, directories kept)`。
- `version`：`sele <版本>` 加 `cli11` / `nlohmann_json` 两行。

```text
Active change: 20261020-my-feature
Tasks: 2/5 done
Decisions: 3
Consistent: yes
```

退出码：0 成功，1 用法错误，2 状态冲突，3 lint 未通过，4 环境错误。

## 变更模型

- id 为 `<YYYYMMDD>-<slug>`，日期取 UTC，slug 限 `[a-z0-9-]`。
- 一次变更同时拥有 `.design/changes/<id>/design.md` 与 `.agent/work/<id>/tasks.md`；两侧必须同时存在，由 `lint` 与 `close` 校验。
- `.agent/STATE.md` 的 `Active Change:` 是唯一指针，`无` 表示无活跃变更；可附 `Phase:` / `Blocked:` / `Next:` 行描述当前阶段、阻塞与去向，工具只改写指针行，其余原样保留。
- 关闭后两侧目录原地保留，历史交给 git；工具不删除、不移动、不归档。

## close 折叠

- 解析 design.md 的 `##` 小节，与 SPEC.md 小节按「标题互为子串」匹配；fenced code 块内的 `## ` 不算小节。
- 命中则替换该小节正文，未命中则在 SPEC.md 末尾追加同名小节；修订式，不追加补丁段。
- 同一份 design.md 内重复的标题合并到同一小节，不会在 SPEC.md 里留下重名节。
- 正文为空或只含 HTML 注释的小节跳过；注释之后还有正文的算内容。
- 每行只右剪空白，保留行首缩进，使嵌套列表与 fenced code 存活。
- 渲染结果与折叠前一致时不重写 SPEC.md（`spec_updated: false`）；需要写入时先写临时文件再改名，避免留下半截文件。
- 写入 SPEC.md 后，仅当 STATE 指针正是该 id 时复位为 `无`；指向其他变更时保持不变。`Phase:` / `Blocked:` / `Next:` 不修改，text 以 `left as-is` 提示。
- `--dry-run` 不写任何文件，按 `replaced` / `appended` / `skipped` 给出计划（text 与 JSON 字段 `plan` 用同一套动作词）；未匹配小节的 `suggested_mapping` 值为空字符串。

## lint 检查

四项：`id_consistency`（changes 与 work 两侧一一对应）、`decisions_append_only`（对比 HEAD 的 `git diff`，出现删除行即失败）、`spec_process_words`、`dir_integrity`（design.md / tasks.md 缺失即失败，`--fix` 按模板补齐）。非 git 仓库或无 HEAD 时 `decisions_append_only` 记 `skip`。

## 版本与安装

`CMakeLists.txt` 的 `project(sele VERSION 0.2.2)` 是版本唯一来源，`configure_file` 生成 `${CMAKE_BINARY_DIR}/generated/sele_version.hpp`；源码中没有版本字面量，改版本只需改这一处并重新配置。

`sele version` 的 json 为 `{"name","version","cli11","nlohmann_json"}`；`--version` 为单行 `sele 0.2.2 (CLI11 2.4.2, nlohmann/json 3.11.2)`，与 `version` 共用 `core::version_display()`。

`install(TARGETS sele RUNTIME DESTINATION bin)`；`SELE_SHARED_DEPS=ON` 时 `cli11_shared`、`nlohmann_json_shared` 的 `RUNTIME` / `LIBRARY` 也装进同一个 `bin/`，使 `$ORIGIN` rpath 与 Windows 同目录 DLL 查找都生效。

## 构建与测试

`cmake -B build -S .`（首次经 FetchContent 拉取依赖，需联网）、`cmake --build build`；产物在嵌套目录 `build/build/sele`。`cmake --install build --prefix <dir>` 装到 `<dir>/bin`。

`ctest --output-on-failure` 运行 20 个用例，工作目录为仓库根；Windows 脚本参数为 `-SeleBin`，Linux 脚本需要 `jq`。用例从 `new` 的 JSON 读取 id，不硬编码日期。
