# 工具与环境

## sele CLI

- 版本 0.2.1，已在 PATH（`D:\Program Files\sele\bin\sele.exe`）。
- 命令文档见 `sele0.2.1.md`；常用：
  - `sele status` 查询活跃变更与决策数
  - `sele lint` 检查 ID 一致性、决策只追加、SPEC 禁过程词、目录完整性
  - `sele new --slug <name>` 创建变更（同时生成 `.design/changes/<id>/design.md` 与 `.agent/work/<id>/tasks.md`）
  - `sele append-decision --context <c> --decision <d> --consequences <e>` 追加决策
  - `sele close <id> --dry-run` 预览折叠，去掉 `--dry-run` 执行
- 在仓库根目录执行；不在根目录时加 `--root <path>`。

## 环境

<!-- 填写：系统、工具、环境、依赖 -->

## 踩坑

- 暂无，遇到问题后追加。
