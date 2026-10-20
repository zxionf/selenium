# 20261020-status-text-output 任务清单

- [x] 步骤 1：`status` 补 text 分支（活跃变更 / 无活跃变更两种形态）
- [x] 步骤 2：`new`、`init`、`append-decision` 补 text 分支
- [x] 步骤 3：`lint`、`close` 补 text 分支（lint 逐项 ok/fail，close 输出预览）
- [x] 步骤 4：`tests/cli_tests.ps1` 与 `tests/cli_tests.sh` 增加 text 模式断言
- [x] 步骤 5：`info.md` 补文本输出说明
- [x] 步骤 6：重建并跑 `ctest`，再手动冒烟六个命令的默认输出
- [x] 步骤 7：对仓库根跑 `sele lint` 确认通过
