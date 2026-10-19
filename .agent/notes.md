## 构建与运行
- 配置：`cmake -B build -S .`（首次会经 FetchContent 拉取 CLI11 v2.4.2 与 nlohmann/json v3.11.2，需要联网）。
- 构建：`cmake --build build`。
- 产物在**嵌套目录**：`build/build/sele.exe`（Windows）/ `build/build/sele`（Linux），不是 `build/`。
- 依赖为 header-only，默认直接编入可执行文件。`-DSELE_SHARED_DEPS=ON` 才把它们封装成共享库（`.dll`/`.so`）动态链接，相关代码在 `src/thirdparty/`。
- 默认未设置 `CMAKE_BUILD_TYPE`，CMake 会加 `-g`，exe 约 1.4 MB；需要更小可加 `-DCMAKE_BUILD_TYPE=Release`。

## 测试（当前已知损坏，别盲信 ctest）
- `ctest --output-on-failure` **无法启动测试**：CMake 传 `-SeleBin`，而 `tests/cli_tests.ps1` 声明的参数是 `-DesignBin`。
- 即使能启动，`tests/cli_tests.ps1` 与 `tests/cli_tests.sh` 硬编码日期 `20261018`，而 ID 用运行时 UTC 日期（`<YYYYMMDD>-<slug>`），非该日期必然失败。
- 手动运行：`powershell -ExecutionPolicy Bypass -File tests/cli_tests.ps1 -DesignBin build/build/sele.exe`。
- 测试脚本直接手工创建 `.design/`/`.agent/` 目录（绕过 `sele init`），因此 `new` 的初始化检查能通过。

## 架构
- `src/main.cpp` 组装 CLI11 app，逐个调用 `cli::register_*(app, global_opts, exit_code)`，顶层 catch 把异常映射为退出码。新增子命令需改 4 处：新建 `src/cli/<cmd>.cpp`、在 `src/cli/commands.hpp` 声明、在 `src/main.cpp` 调用、在 `CMakeLists.txt` 加源文件。
- 子命令回调在 `app.parse()` 时才执行（`register_*` 已返回），所以选项状态用 `std::shared_ptr` 堆分配并按值捕获；只有 `GlobalOptions&` 和 `int& exit_code` 按引用捕获（生命周期陷阱）。
- `src/core/`：根目录发现、ID 生成/校验、模板渲染、git 子进程；`src/util/`：输出与退出码。
- `sele init` 负责创建 `.design/`+`.agent/`；`sele new` 在未初始化时拒绝执行。仓库根目录自己的 `.design/`/`.agent/` 是工具自举用（未被 `.gitignore` 忽略，`.gitignore` 只忽略 `build/` 与 `test_proj/`）。
- `--version` 经 `src/thirdparty/deps.hpp` 读取依赖版本，两种链接模式都可用。

## 坑
- **不要把共享头文件命名为 `src/cli/cli.hpp`**：在大小写不敏感的文件系统上会让 `#include <CLI/CLI.hpp>` 解析到它并导致构建失败。因此该头文件叫 `commands.hpp`。
- `GlobalOptions` / `add_global_opts` 声明在 `src/cli/commands.hpp`、定义在 `src/cli/commands.cpp`。
- lint 会拒绝 `.design/SPEC.md` 中出现 `TODO`、`WIP`、`待办`、`下一步`、`进行中`（忽略大小写）；写 `src/core/template.cpp` 的模板文本时别带上这些词。