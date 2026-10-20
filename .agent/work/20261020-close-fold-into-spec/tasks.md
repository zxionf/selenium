# 20261020-close-fold-into-spec 任务清单

- [x] 步骤 1：`close.cpp` 增加 design.md / SPEC.md 的 `##` 小节解析与渲染辅助函数
- [x] 步骤 2：`--dry-run` 改为解析真实小节，未匹配映射值置空并列出候选
- [x] 步骤 3：实现真正的折叠（命中替换正文、未命中追加小节、空正文跳过）
- [x] 步骤 4：`STATE.md` 仅在指针命中时复位为“无”，两侧目录原地保留
- [x] 步骤 5：更新 `tests/cli_tests.ps1` 与 `cli_tests.sh`（改写 close 用例 + 新增 2 条）
- [x] 步骤 6：`info.md` 更新 close 语义与退出码
- [x] 步骤 7：重建、跑 `ctest`、手动冒烟 dry-run 与实际关闭
- [x] 步骤 8：用新实现关闭 `20261020-status-text-output` 并确认 `SPEC.md` 折入
- [x] 步骤 9：`sele lint` 通过并追加决策记录
