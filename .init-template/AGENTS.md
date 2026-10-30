<!-- 本文件是 sele init 内置模板的参考副本；修改时请同步 src/core/template.cpp 的 render_agents_template()。 -->

# AGENTS.md

project-name

本文档不允许 ai 修改；确需修改必须由用户发起。

## 入口

- **agent 契约：`.sele/CONTRACT.md`（不可违反清单，优先阅读）**。
- 首次使用：`sele init`（创建 `.sele/`；若本文件缺失会一并生成）。
- 当前状态：`sele overview --output json`。
- `.sele/` 是工具私有目录，只通过 `sele` 命令读写；不要用编辑器直接修改。
- 命令细节：`sele --help`、`sele line --help`、`sele task --help`。

## 最小闭环

```bash
sele line new <slug> --goal "..." --acceptance "desc::check" --modules a,b
sele task add "..." --modules a,b
sele line open <id>
sele task start T1 && sele task checkpoint T1 --msg "..." && sele task done T1
sele acceptance pass A1 --evidence "..."
sele line done <id>
sele line merge <id> --confirm
```

规则：`task done` 前必须有 `checkpoint`；`line done` 前所有 task 与 acceptance 必须完成；`.sele/` 的结构只由命令写。
