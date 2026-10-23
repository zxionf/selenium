# Agent Index

<!-- agent 工作区索引：列出契约、状态与任务入口 -->

## 冷启动顺序

1. 本文件
2. `STATE.md`：当前阶段、活跃变更、阻塞项
3. `.design/INDEX.md`：按需取设计文档
4. 有活跃变更时：`.agent/work/<id>/tasks.md` → `handover.md`

## 文件

- `CONTRACT.md` 操作契约与硬约束（必读）
- `STATE.md` 当前状态指针（极短）
- `notes.md` 工具、环境、踩坑（只追加）
- `sele0.2.2.md` sele CLI 文档
- `work/<id>/tasks.md` 变更任务与进度
- `work/<id>/handover.md` 会话交接（每次结束覆写）
