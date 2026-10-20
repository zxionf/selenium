## 构建与运行
- 配置：`cmake -B build -S .`（首次会经 FetchContent 拉取 CLI11 v2.4.2 与 nlohmann/json v3.11.2，需要联网）。
- 构建：`cmake --build build`。
- 产物在**嵌套目录**：`build/build/sele.exe`（Windows）/ `build/build/sele`（Linux），不是 `build/`。
- 依赖为 header-only，默认直接编入可执行文件。`-DSELE_SHARED_DEPS=ON` 才把它们封装成共享库（`.dll`/`.so`）动态链接，相关代码在 `src/thirdparty/`。
- 默认未设置 `CMAKE_BUILD_TYPE`，CMake 会加 `-g`，exe 约 1.4 MB；需要更小可加 `-DCMAKE_BUILD_TYPE=Release`。

## 测试
- `ctest --output-on-failure` 可用（14 个用例，工作目录为仓库根）。
- 用例 ID 不再硬编码：从 `new` 的 JSON 输出读取 `.id` 再用于路径与断言，跨 UTC 日期运行稳定。
- 默认（text）输出非空由两个用例守护：六个子命令若只剩 `print_json`，会立刻失败。新命令的 text 形态用 `Output::print_both(text, json)` 渲染同一份数据。
- close 用例覆盖真实折叠：命中 SPEC 小节时替换正文、未命中时追加、关闭非活跃变更不得改动 `STATE.md`，另有一条断言嵌套列表缩进必须存活。
- ps1 脚本参数名为 `-SeleBin`，与 CMakeLists 的调用一致。手动运行：`powershell -ExecutionPolicy Bypass -File tests/cli_tests.ps1 -SeleBin build/build/sele.exe`。
- Linux 手动运行：`bash tests/cli_tests.sh build/build/sele`，需要 `jq`。脚本用了 `sed -i` 与 `stat -c %Y`（GNU 语法），macOS 上需先换用 `gsed`/`gstat`。
- 测试脚本直接手工创建 `.design/`/`.agent/` 目录（绕过 `sele init`），因此 `new` 的初始化检查能通过。

## 架构
- `src/main.cpp` 组装 CLI11 app，逐个调用 `cli::register_*(app, global_opts, exit_code)`，顶层 catch 把异常映射为退出码。新增子命令需改 4 处：新建 `src/cli/<cmd>.cpp`、在 `src/cli/commands.hpp` 声明、在 `src/main.cpp` 调用、在 `CMakeLists.txt` 加源文件。
- 子命令回调在 `app.parse()` 时才执行（`register_*` 已返回），所以选项状态用 `std::shared_ptr` 堆分配并按值捕获；只有 `GlobalOptions&` 和 `int& exit_code` 按引用捕获（生命周期陷阱）。
- `src/core/`：根目录发现、ID 生成/校验、模板渲染、git 子进程；`src/util/`：输出与退出码。
- `sele init` 负责创建 `.design/`+`.agent/`；`sele new` 在未初始化时拒绝执行。仓库根目录自己的 `.design/`/`.agent/` 是工具自举用（未被 `.gitignore` 忽略，`.gitignore` 只忽略 `build/` 与 `test_proj/`）。
- `--version` 经 `src/thirdparty/deps.hpp` 读取依赖版本，两种链接模式都可用。

## 坑
- **不要把共享头文件命名为 `src/cli/cli.hpp`**：在大小写不敏感的文件系统上会让 `#include <CLI/CLI.hpp>` 解析到它并导致构建失败。因此该头文件叫 `commands.hpp`。
- `close` 折叠 SPEC.md 时每行只做右剪空白：若用 `trim`，嵌套列表与 fenced code 的缩进会被压平（已踩过一次）。
- 在 PowerShell 5.1 里给 `sele` 传含中文的参数时别用全角引号 `“ ”`：PowerShell 会把它们当引号，参数被截断，CLI11 报 `argument was not expected`。改用单引号或 `「 」`。
- `GlobalOptions` / `add_global_opts` 声明在 `src/cli/commands.hpp`、定义在 `src/cli/commands.cpp`。
- lint 会拒绝 `.design/SPEC.md` 中出现 `TODO`、`WIP`、`待办`、`下一步`、`进行中`（忽略大小写）；写 `src/core/template.cpp` 的模板文本时别带上这些词。