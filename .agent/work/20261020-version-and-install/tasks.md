# 20261020-version-and-install 任务清单

- [x] 步骤 1：`CMakeLists.txt` 加 `project(VERSION 0.2.0)`、`configure_file` 生成版本头、include 目录
- [x] 步骤 2：新增 `src/core/version.hpp/.cpp`（版本串与展示串）
- [x] 步骤 3：新增 `src/cli/version.cpp`（`version` 子命令，text/json 双形态）
- [x] 步骤 4：`main.cpp` 的 `--version` 改用 `core::version_display()`，并注册新子命令
- [x] 步骤 5：`CMakeLists.txt` 加 `install()` 规则（含共享依赖模式）
- [x] 步骤 6：测试新增 version 用例（json 字段、text 非空、与 `--version` 一致），14 → 15
- [x] 步骤 7：`info.md` 补 version 命令与安装章节
- [x] 步骤 8：重建、`ctest`、实跑 `cmake --install` 到临时前缀并验证装出的 exe 可运行
- [x] 步骤 9：`sele lint` 通过并追加决策记录
