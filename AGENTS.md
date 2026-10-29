# AGENTS.md

`sele`：管理 `.sele/` 线任务体系的 C++20 CLI（line → task → acceptance → merge），同时保留 `.design/` + `.agent/` 旧体系。

本文档不允许 ai 修改；确需修改必须由用户发起。

## 入口

1. 命令、状态机、JSON 契约与常见错误：`docs/sele-v0.10.0.md`。
2. 当前状态：`sele overview --output json`。
3. `.sele/` 是工具私有目录，只通过 `sele` 命令读写；不要用编辑器直接修改。
4. `.design/` 与 `.agent/` 是旧体系（legacy），仅作历史参考；新工作不要往里写。

## 体系最小闭环

```bash
sele init
sele line new <slug> --goal "..." --acceptance "desc::check" --modules a,b
sele task add "..." --modules a,b
sele line open <id>
sele task start T1 && sele task checkpoint T1 --msg "..." && sele task done T1
sele acceptance pass A1 --evidence "..."
sele line done <id>
sele line merge <id> --confirm
```

规则：`task done` 前必须有 `checkpoint`；`line done` 前所有 task 与 acceptance 必须完成；`.sele/` 的结构只由命令写。
