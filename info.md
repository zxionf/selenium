# sele CLI

一个用于管理 `.design/` 与 `.agent/` 目录的 C++20 命令行工具，支撑「创建变更 → 追加决策 → 查询状态 → 规则检查 → 归档预览」这一最小闭环。

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

## 命令

所有命令均支持以下全局选项：

- `--root <path>` — 项目根目录（默认：当前目录，并向上查找 `.design/`）
- `--output text|json` — 输出格式（默认：text）

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

### `sele close <id> --dry-run`

预览关闭一次变更（MVP 仅实现 `--dry-run`）。

```bash
sele close 20261018-my-feature --dry-run
```

退出码：0=成功，1=缺少 --dry-run，2=ID 不存在，4=环境错误

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
├── cli/                  # 各子命令实现
│   ├── commands.hpp/.cpp # 共享的 GlobalOptions 与子命令注册声明
│   ├── init.cpp
│   ├── new.cpp
│   ├── append_decision.cpp
│   ├── status.cpp
│   ├── lint.cpp
│   └── close.cpp
├── core/                 # 核心逻辑
│   ├── root.hpp/.cpp     # 查找 .design/ 与 .agent/ 根目录
│   ├── id.hpp/.cpp       # slug 校验、ID 生成与解析
│   ├── template.hpp/.cpp # design.md / tasks.md / DECISIONS.log 模板渲染
│   └── git.hpp/.cpp      # git 调用与仓库状态探测
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

覆盖 10 个测试用例，涉及正常路径与错误场景。

> 说明：测试用例中的 ID 在运行时从 `new` 的 JSON 输出读取（不再硬编码日期），因此跨日期运行不会失败。Windows 下手动运行：
>
> ```powershell
> powershell -ExecutionPolicy Bypass -File tests/cli_tests.ps1 -SeleBin build/build/sele.exe
> ```
>
> Linux/macOS 下为 `bash tests/cli_tests.sh build/build/sele`（需要 `jq`）。

## 决策

实现过程中的决策记录见 `.design/DECISIONS.log`。
