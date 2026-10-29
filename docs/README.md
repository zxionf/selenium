# docs 索引

本目录存放面向开发者的项目文档。设计真相源仍在 `.design/SPEC.md`，这里只做**实现层**的梳理与导读。

| 文件 | 一句话 | 何时读 |
|---|---|---|
| `architecture.md` | 代码分层、模块职责、命令数据流、`close` 折叠算法、构建与测试、扩展指南（描述 0.2.2 legacy 层） | 接手或改造旧代码前 |
| `sele-e.md` | 多线（line）+ 任务 + 检查点的设计草案 | 讨论新体系时 |
| `sele-e-review.md` | 对 `sele-e.md` 的评审：P0/P1 问题、`.git` 类比边界、待决策 | 修订设计前 |
| `sele-v1-spec.md` | **v0.3-v0.10 实现规格**：`.sele` 布局、数据模型、状态机、命令、并发、双模式 | 实现或使用新体系时 |
| `sele-v0.10.0.md` | **面向 agent 的 0.10.0 使用手册**：工作流、命令表、状态机、JSON 契约、常见错误 | agent 冷启动 / 操作 `.sele` 项目时 |

## 与其它文档的分工

- `.design/SPEC.md`：对外契约（命令表面、JSON 字段、退出码、目录模型）的**权威**描述。
- `.design/DECISIONS.log`：为什么这样设计（只追加）。
- `.agent/notes.md`：构建命令、踩坑、环境差异。
- `docs/architecture.md`：代码怎么组织、一次调用怎么走、扩展要改哪里。
- `docs/sele-v1-spec.md`：`.sele` 体系的实现依据（v0.3 起）。

三处若有冲突，以 `.design/SPEC.md` 为准，并在随后变更中同步修正本文。
