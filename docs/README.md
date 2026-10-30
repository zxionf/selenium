# docs 索引

本目录存放项目文档。实现契约以 `docs/sele-v1-spec.md` 为准；人类上手看 `sele-user-guide.md`，agent 看 `sele-v0.10.0.md`。

| 文件 | 一句话 | 何时读 |
|---|---|---|
| `architecture.md` | 代码分层、模块职责、命令数据流、`close` 折叠算法、构建与测试、扩展指南（描述 0.2.2 legacy 层） | 接手或改造旧代码前 |
| `sele-e.md` | 多线（line）+ 任务 + 检查点的设计草案 | 讨论新体系时 |
| `sele-e-review.md` | 对 `sele-e.md` 的评审：P0/P1 问题、`.git` 类比边界、待决策 | 修订设计前 |
| `sele-v1-spec.md` | **v0.3-v0.11 实现规格**：`.sele` 布局、数据模型、状态机、命令、并发、双模式 | 实现或使用新体系时 |
| `sele-v0.10.0.md` | **面向 agent 的使用手册**：工作流、命令表、状态机、JSON 契约、常见错误 | agent 冷启动 / 操作 `.sele` 项目时 |
| `sele-user-guide.md` | **人类使用指南**：安装、快速开始、概念、工作流、命令速查、FAQ | 第一次使用 sele、日常查阅 |
| `sele-config.md` | **配置文档**：config 全字段、环境变量、提交信息规则、示例、迁移 | 调整提交语言/scope/白名单时 |
| `sele-git.md` | sele 与 git 的协作：提交时刻表、分支模型、干净判定、冲突/回退语义、已知边界 | 修改 git 相关逻辑或排查状态不一致时 |

## 与其它文档的分工

- `docs/sele-v1-spec.md`：实现契约（状态机、JSON、退出码、目录模型）的**权威**描述。
- `docs/sele-git.md`：sele 与 git 的提交时刻表、干净判定、回退/冲突语义。
- `docs/sele-v0.10.0.md`：agent 操作手册（命令细节、JSON 契约、常见错误）。
- `docs/sele-user-guide.md`：人类上手与日常使用。
- `docs/sele-config.md`：配置项与提交信息规则。
- `docs/architecture.md`：0.2.2 legacy 层的历史说明（部分已过时）。
- `.design/` 与 `.agent/`：旧体系历史，0.11 起工具不再识别。

冲突时以 `docs/sele-v1-spec.md` 为准，并在随后变更中同步修正本文。
