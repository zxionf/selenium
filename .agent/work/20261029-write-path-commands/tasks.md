# 20261029-write-path-commands 任务清单

- [ ] 步骤 1：新增 `src/core/fs.hpp/.cpp`，把 `close.cpp` 的 `read_file` / `write_file`（临时文件 + `flush` + 状态检查 + `rename`）原样抽为 `core::fs::read_file` / `write_atomic`；`close.cpp` 改为引用，行为不变
- [ ] 步骤 2：新增 `src/core/state.hpp/.cpp`（`read_fields` / `apply_fields`，按行编辑、保留未知行与注释、统一 `kActiveChange` 常量）；`new.cpp`、`close.cpp`、`status.cpp` 三处内联逻辑改为调用，删除重复实现
- [ ] 步骤 3：`core/template.cpp` 增加 `render_handover_template(id)`（三段：已完成 / 未完成 / 下一步，各带占位注释）；`new.cpp` 生成 `handover.md`，JSON 增加 `handover_path`，text 明细增加一行 handover
- [ ] 步骤 4：新增 `src/cli/task.cpp`：`task list|add|done|undone`，`<id>` 可省略取 Active Change，序号按全文 checkbox 顺序，`done/undone` 幂等，越界退 2，`--text` 为空退 1，`tasks.md` 缺失时 `add` 按模板创建
- [ ] 步骤 5：新增 `src/cli/state.cpp`：`state show`（缺失值显示 `-`）、`state set --phase/--blocked/--next`（只改给定键、空串写 `无`、无参数退 1、幂等 `changed:false`），不提供 `Active Change` 写入入口
- [ ] 步骤 6：新增 `src/cli/handover.cpp`：`handover write`（结构化三段 / `--from-file -|path` 全文，互斥，无内容退 1，相同内容 `changed:false`，`--dry-run` 只预览）、`handover show`
- [ ] 步骤 7：新增 `--from-file` 文本读取助手（`-` 读 stdin，其余按路径读，读取失败退 4），供 `handover write` 的整文模式使用；与三段结构化参数互斥
- [ ] 步骤 8：`Output::error` 增加可选 `next` 数组参数（默认空，JSON 输出新字段，既有人类可读文本不变）；`task` / `state` / `handover` 的缺失活跃变更路径带上可执行 `next`
- [ ] 步骤 9：接线：`src/cli/commands.hpp` 增加三个 `register_*` 声明，`src/main.cpp` 调用，`CMakeLists.txt` 加入 5 个新源文件
- [ ] 步骤 10：测试：`tests/cli_tests.sh` 与 `tests/cli_tests.ps1` 各新增 12 条用例（task 往返/幂等/越界/缺 id、state set/show/保留未知行、handover 写入/相同内容不写/dry-run、new 生成 handover、--from-file 走 stdin、三个新命令的默认 text 非空），总数 20 → 32
- [ ] 步骤 11：更新 `info.md` 的"命令"与"项目结构"两节（新命令、新源文件、`--from-file`、通用 JSON 字段 `command/changed/path`）
- [ ] 步骤 12：构建并验证：`cmake --build build`、`ctest --output-on-failure` 全绿、`./build/build/sele lint` 通过、`bash tests/cli_tests.sh build/build/sele` 通过
- [ ] 步骤 13：向 `.design/DECISIONS.log` 追加决策（结构写入单点、幂等语义、不提供 `Active Change` 写入入口、并发保护为尽力而为），每条带 `--change-id 20261029-write-path-commands`
- [ ] 步骤 14：`sele close 20261029-write-path-commands --dry-run` 确认五个小节均为 `replaced`；执行 `close`；用 `git diff .design/SPEC.md` 只读复核 SPEC 未被破坏、`sele lint` 仍通过
