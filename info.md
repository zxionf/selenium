# sele CLI

> **v0.3（当前）**：新增 `.sele/` 线任务体系——`line` / `task` / `acceptance` / `overview` / `log` / `fsck`。`sele init` 默认创建 `.sele/`；旧的 `.design/.agent` 体系保留，用 `sele init --legacy` 创建。两套命令按项目类型分派，规格见 [`docs/sele-v1-spec.md`](docs/sele-v1-spec.md)。下文“命令”一节描述的是 v0.2.2 legacy 体系，仍然可用。

## v0.3 线任务体系（摘要）

```bash
sele init                                                        # 创建 .sele/
sele line new fix-login --goal "登录失败时给出可操作错误" \
  --acceptance "错误密码返回 401::pytest tests/test_login.py" --modules auth
sele task add "复现 401 分支" --modules auth
sele line open <id>                                              # 冻结验收、创建 sele/<id> 分支
sele task start T1
sele task checkpoint T1 --msg "wip"                              # 唯一的自动 git 提交
sele task done T1
sele acceptance pass A1 --evidence "pytest 12 passed"            # 只记账，不执行命令
sele line done <id>
sele line merge <id> --confirm                                      # 合并进 main（冲突则 abort 并退回 done）
sele line revert <id> --to T1                                      # 回退到 T1 的 checkpoint（T2 起重置为 pending）
sele task revert T2                                                # 撤销 T2 本身（回到 T1 完成时）
sele line rebase <id>                                              # 变基到最新 main；冲突时用 --continue / --abort
sele worktree add <id> --path ../wt-<id>                           # 为线创建独立工作树
sele log --events                                                 # 合并 task 过程事件查看
sele overview
```

目录：`.sele/lines/<id>/{line.json,tasks/*.json}` 与 `.sele/log.jsonl` 入库；`.sele/runtime/`（会话指针、task 状态、细则事件）本机忽略。任务状态机为线内严格串行，跨 line 可并存（v0.3 为单写者，不做 fork/join、worktree、merge）。

## 构建

```bash
# 依赖：CMake ≥ 3.20、C++20 编译器
cmake -B build -S .
cmake --build build
```

生成的二进制文件位于 `build/build/sele`（Windows 下为 `build/build/sele.exe`）。

依赖项（CLI11、nlohmann/json）通过 CMake FetchContent 自动拉取，二者均为 header-only。

默认（`SELE_SHARED_DEPS=OFF`）直接将其包含进程序内部，不生成任何共享库：

```bash
cmake -B build -S .
cmake --build build
```

如需改为动态链接，可开启选项，将两个依赖分别封装为共享库 `cli11_shared`、`nlohmann_json_shared`（导出 `sele_cli11_version()`、`sele_nlohmann_json_version()`）再链接：

```bash
cmake -B build -S . -DSELE_SHARED_DEPS=ON
cmake --build build
```

开启后，Windows 下会在与 `sele.exe` 相同的输出目录生成 `libcli11_shared.dll`、`libnlohmann_json_shared.dll`（Linux 下为 `lib*.so`，通过 `$ORIGIN` rpath 定位）。无论哪种模式，都可用 `sele --version` 查看 `sele` 及依赖库版本。

> 说明：这两个库是 header-only，其模板代码无论如何都会随头文件编译进 `sele.exe`；共享库仅承载版本符号，用于建立并验证运行时的动态依赖关系。

## 安装

版本号由 `CMakeLists.txt` 的 `project(sele VERSION 0.2.2)` 单一决定，`configure_file` 生成 `sele_version.hpp` 后编进二进制；源码里没有版本字面量，改版本只改这一处，然后重新配置构建。

CMake 提供了 `install()` 规则：`sele` 会被安装到前缀下的 `bin/`。前缀随意指定，不需要管理员权限：

```bash
# 装到你自己的目录（推荐，永不污染系统）
cmake --install build --prefix "$HOME/.local"

# 或者装到系统目录（Linux/macOS 通常需要 sudo）
sudo cmake --install build --prefix /usr/local
```

```powershell
# Windows：装到用户级目录，无需管理员
cmake --install build --prefix "$env:LOCALAPPDATA\Programs\sele"
```

装完后二进制位于 `<prefix>/bin/sele`（Windows 为 `<prefix>\bin\sele.exe`）。`SELE_SHARED_DEPS=ON` 模式下，`cli11_shared` / `nlohmann_json_shared` 也会一并装进同一个 `bin/`，保证 `$ORIGIN`（Linux）与同目录 DLL 查找（Windows）都能命中。

让命令进入 `PATH`：

```powershell
# Windows：用户级 PATH 追加（重启终端后生效）
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";$env:LOCALAPPDATA\Programs\sele\bin", "User")
```

```bash
# Linux/macOS：bash/zsh 下写入 shell 配置
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc   # 或 ~/.zshrc
```

验证安装：

```bash
sele version
# sele 0.2.2
#   cli11: 2.4.2
#   nlohmann_json: 3.11.2
```

不想用 `cmake --install` 也可以直接复制单文件（默认模式只产出一个 exe，无运行时依赖）：

```powershell
Copy-Item build\build\sele.exe "$env:LOCALAPPDATA\Programs\sele\bin\sele.exe"
```

## 命令

所有命令均支持以下全局选项：

- `--root <path>` — 项目根目录（默认：当前目录，并向上查找 `.design/`）
- `--output text|json` — 输出格式（默认：text）

### 输出格式

`--output text`（默认）面向人，打印一段摘要；`--output json` 面向程序，字段固定。两者由同一份数据渲染，内容一致。

```text
$ sele status
Active change: 20261020-my-feature
Tasks: 2/5 done
Decisions: 3
Consistent: yes
```

`STATE.md` 若带有 `Phase:` / `Blocked:` / `Next:` 行，`status` 会在摘要末尾各追加一行（没有就不显示）；JSON 中对应 `phase` / `blocked` / `next` 三个字段，缺失时为空字符串。

```text
$ sele lint
Lint failed
  [ok] id_consistency
  [skip] decisions_append_only
  [fail] spec_process_words
  [ok] dir_integrity
```

其余命令的 text 形态：`init` 输出 `Initialized ...` 加 design/agent 路径，`new` 输出 `Created <id>` 加两侧路径，`append-decision` 输出 `Appended decision #<n> -> <path>`，`version` 输出 `sele <版本>` 加两行依赖版本；`close --dry-run` 输出 `Close preview for <id> ...` 加折叠计划，实际关闭输出 `Closed <id> (spec folded, directories kept)` 加每个小节的 action 与指针是否复位。

### `sele version [--output json]`

查看版本。不需要 `.design/`，任意目录可运行；`--version` 与它同源。

```bash
sele version
sele version --output json
sele --version                 # 单行：sele 0.2.2 (CLI11 2.4.2, nlohmann/json 3.11.2)
```

```json
{"name":"sele","version":"0.2.2","cli11":"2.4.2","nlohmann_json":"3.11.2"}
```

退出码：0=成功

### `sele init [directory]`

初始化项目，创建 `.design/`、`.agent/` 目录及其模板文件（INDEX.md、SPEC.md、DECISIONS.log、CONTRACT.md、STATE.md 等）。

```bash
sele init                 # 默认使用当前工作目录
sele init ./my-project    # 指定目录（位置参数）
sele init --root ./my-project  # 也可用 --root 指定
```

- 若目标目录已存在 `.design/` 或 `.agent/`，则报错退出（不会覆盖）。
- 退出码：0=成功，2=已初始化，4=环境错误

### `sele new --slug <name>`

创建一次新的变更，同时生成 design 与 work 目录。

> 必须先执行 `sele init` 完成初始化，否则 `new` 会报错并提示先初始化。

```bash
sele new --slug my-feature
```

退出码：0=成功，1=slug 非法，2=ID 已存在，4=未初始化或环境错误

### `sele append-decision --context <c> --decision <d> --consequences <e> [--change-id <id>]`

向 `.design/DECISIONS.log` 追加一条决策记录。

```bash
sele append-decision --context "选用 C++20" --decision "使用 modules" --consequences "导入更简洁" --change-id 20261018-my-feature
```

退出码：0=成功，1=缺少参数，4=环境错误

### `sele status [--output json]`

查看当前活动变更的状态（只读、快速）。

```bash
sele status --output json
```

退出码：0=成功，4=环境错误

### `sele lint [--fix]`

运行机械化的规则检查：

1. ID 双向一致性（`.design/changes/<id>` 与 `.agent/work/<id>` 互相对应）
2. DECISIONS.log 只允许追加（通过 git diff 判断）
3. SPEC.md 不得包含过程性词汇（TODO、WIP、待办、下一步、进行中）
4. 目录完整性（design.md、tasks.md 是否存在）

```bash
sele lint
sele lint --fix  # 自动补齐缺失的模板文件
```

退出码：0=全部通过，3=存在检查失败，4=环境错误

### `sele close <id> [--dry-run]`

关闭一次变更：把 `.design/changes/<id>/design.md` 的各 `##` 小节折叠进 `.design/SPEC.md`。

- 命中 SPEC.md 已有同名（互为子串）小节 → **替换该小节正文**（修订式折叠，不追加补丁段）。
- 未命中 → 在 SPEC.md 末尾**追加新小节**；同一份 design.md 内重复的标题合并到同一小节。
- 正文为空或只含 HTML 注释的小节跳过，不写入 SPEC.md；注释后面还有正文的按内容处理。
- fenced code 块里的 `## ` 行不算小节，代码块整体原样保留。
- `.design/changes/<id>/` 与 `.agent/work/<id>/` **原地保留**，历史交给 git。
- 仅当 `STATE.md` 的 `Active Change:` 正是该 id 时复位为 `无`；指向其他变更时保持不变。
- `Phase:` / `Blocked:` / `Next:` 行不修改，原样保留；存在这些行时文本输出以 `Phase/Blocked/Next left as-is` 提示。
- 折叠结果与折叠前一致时不重写 SPEC.md（JSON `spec_updated` 为 `false`），重复关闭不会产生无意义的 diff。

```bash
sele close 20261018-my-feature --dry-run   # 只预览折叠计划，不写任何文件
sele close 20261018-my-feature             # 执行折叠并复位指针
```

`--dry-run` 输出真实解析出的小节（不再用写死的标题），匹配不到时映射值为空字符串；计划用 `replaced` / `appended` / `skipped` 一套动作词，文本与 JSON 的 `plan` 字段一致。

退出码：0=成功，1=缺少参数，2=两侧目录或 design.md 缺失，4=环境错误

## 退出码

| 代码 | 含义 |
|------|------|
| 0 | 成功 |
| 1 | 用法错误（缺少或非法参数） |
| 2 | 状态冲突（ID 已存在、ID 不存在） |
| 3 | lint 检查未通过 |
| 4 | 环境错误（目录不存在、git 不可用、权限不足） |

## JSON 输出

每个命令都支持 `--output json`，字段名稳定，可被 `jq` 解析。

示例：

```json
{"id":"20261018-my-feature","design_path":".design/changes/.../design.md","work_path":".agent/work/.../tasks.md","state_updated":true}
```

## 项目结构

```
src/
├── main.cpp              # 入口：解析全局选项、注册子命令、统一错误处理
├── version.hpp.in        # 版本头模板，configure_file 生成 sele_version.hpp
├── cli/                  # 各子命令实现
│   ├── commands.hpp/.cpp # 共享的 GlobalOptions 与子命令注册声明
│   ├── init.cpp
│   ├── new.cpp
│   ├── append_decision.cpp
│   ├── status.cpp
│   ├── lint.cpp
│   ├── close.cpp
│   └── version.cpp
├── core/                 # 核心逻辑
│   ├── root.hpp/.cpp     # 查找 .design/ 与 .agent/ 根目录
│   ├── id.hpp/.cpp       # slug 校验、ID 生成与解析
│   ├── template.hpp/.cpp # design.md / tasks.md / DECISIONS.log 模板渲染
│   ├── git.hpp/.cpp      # git 调用与仓库状态探测
│   └── version.hpp/.cpp  # 版本串与含依赖版本的展示串
└── util/                 # 通用工具
    ├── output.hpp/.cpp   # text/json 输出与错误输出
    └── exit_codes.hpp    # 退出码常量
```

被管理的目录结构：

```
.design/
├── INDEX.md
├── SPEC.md
├── DECISIONS.log
└── changes/
    └── <id>/
        └── design.md
.agent/
├── INDEX.md
├── CONTRACT.md
├── STATE.md
└── work/
    └── <id>/
        └── tasks.md
```

ID 格式：`<YYYYMMDD>-<slug>`（例如 `20261018-my-feature`）。

## 测试

```bash
ctest --output-on-failure
```

覆盖 20 个测试用例，涉及正常路径、错误场景、text 输出、close 折叠（重复标题、注释正文、代码块）、STATE 字段透传、版本查询。

> 说明：测试用例中的 ID 在运行时从 `new` 的 JSON 输出读取（不再硬编码日期），因此跨日期运行不会失败。Windows 下手动运行：
>
> ```powershell
> powershell -ExecutionPolicy Bypass -File tests/cli_tests.ps1 -SeleBin build/build/sele.exe
> ```
>
> Linux/macOS 下为 `bash tests/cli_tests.sh build/build/sele`（需要 `jq`）。

## 决策

实现过程中的决策记录见 `.design/DECISIONS.log`。
