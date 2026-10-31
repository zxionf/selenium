# selenium

`sele` 是一个面向 AI agent 的 C++20 命令行工具，用「计划（Plan）→ 线（Line）→ 任务（Task）→ 步骤（Step）」组织开发工作。

它只做三件事：**记录意图、记录进度、留下证据**。所有状态存放在工具私有目录 `.sele/`（类比 `.git/`），只能通过 `sele` 命令读写。

> 当前版本 **0.21.0**。**绝不向后兼容**：pre-0.20 的 `.sele` 布局（`tasks/`、`checkpoints/`、
> `epics/`、`session`、`runtime`、`lines/<id>/`）会被直接拒绝（退出码 4），不提供迁移；旧命令已删除。

## 特性

- **四层结构**：Plan 带验收与审批，Line 是执行线，Task 是功能节点，Step 是内嵌待办。
- **三个不变量**：全局至多一个进行中 Task（I1）；单 Task 至多一个进行中 Step（I2）；每次迁移都写事件（I3）。
- **显式状态机**：迁移前置条件集中校验，非法迁移直接拒绝。
- **暂停是一等状态**：`pause_context` 保存步骤、git 快照、恢复提示，恢复时不依赖记忆。
- **审核分层**：`strict` / `lenient` / `mixed`，未满足时自动转 `pending_review`。
- **可审计**：`log.jsonl` append-only，`sele log` 可查。
- **可脚本化**：所有命令支持 `--root` 与 `--output text|json`，退出码固定。

## 安装

依赖：C++20 编译器、CMake 3.20+；CLI11 与 nlohmann/json 由 CMake FetchContent 拉取。

```bash
cmake -B build -S .
cmake --build build
cmake --install build --prefix "$HOME/.local"
export PATH="$HOME/.local/bin:$PATH"
sele version
```

构建产物在 `build/build/sele`，不安装也可以直接使用。

## 快速开始

```bash
sele init

# 计划：目标 + 计划级验收 + 声明的线
sele plan new demo --goal "用户能登录" --acceptance "端到端可用" --lines auth
sele plan submit demo
sele plan approve demo --by human:you

# 线：在 active plan 下创建
sele line new auth --plan demo --goal "提供认证能力" --acceptance "登录接口可用" --modules core

# 任务与步骤
sele task add "实现登录接口" --line auth --acceptance "登录成功返回 token"
sele task start T1 --line auth
sele step add T1 "设计 schema" --line auth
sele step add T1 "实现接口" --line auth
sele step start T1 S1 --line auth
sele step done T1 S1 --line auth
sele step start T1 S2 --line auth
sele step done T1 S2 --line auth

# 验收与完成
sele task verify T1 A1 --line auth --by human:you
sele task done T1 --line auth
sele line verify auth A1 --plan demo --by human:you
sele line done auth --plan demo
sele plan verify demo PA1 --by human:you
sele plan done demo
```

## 核心概念

| 概念 | 含义 |
|---|---|
| plan | 计划：目标、计划级验收、审批状态 |
| line | 执行线：一个目标 + 线级验收 + 允许的模块 |
| task | 功能节点：内嵌多个 step 与验收点 |
| step | 待办事项：顺序执行，可跳过、可跳转 |
| event | `log.jsonl` 中的一条迁移记录，append-only |
| paused / blocked / pending_review | 暂停 / 阻塞 / 待人工审核 |

## 命令

```text
sele init                      初始化 .sele/
sele plan   new|submit|approve|reject|done|verify|status|show|archive|restore|list
sele line   new|activate|switch|status|verify|done|abandon|dep-add|dep-remove|check|list
sele task   add|start|pause|resume|done|approve|fail|retry|block|unblock|verify|archive|revive|list
sele step   add|start|done|skip|goto|approve|reject|list
sele next / status / tree / log / check
sele git    status|diff|log|commit
sele version
```

完整参数见 `sele <命令> --help`；实现说明见 [`docs/sele-0.20.0.md`](docs/sele-0.20.0.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/sele-f.md`](docs/sele-f.md) | 接口契约：命令、字段、状态、目录 |
| [`docs/sele-f-d.md`](docs/sele-f-d.md) | 概念模型：为什么这样设计、边界 |
| [`docs/sele-0.20.0.md`](docs/sele-0.20.0.md) | 0.20.0 实际实现、状态机、范围裁剪 |
| [`docs/README.md`](docs/README.md) | 文档索引 |

## 开发

```bash
cmake -B build -S .
cmake --build build
bash tests/sele_v2_tests.sh build/build/sele
ctest --test-dir build --output-on-failure
```

- 源码：`src/cli/`（子命令）、`src/core/`（实体、存储、事件、状态机）、`src/util/`（输出与退出码）。
- 测试：`tests/sele_v2_tests.sh`（端到端，临时目录 + JSON 断言）。
- 构建开启 `-Wall -Wextra -Wpedantic -Werror`，警告即错误。

本仓库用于探索人类与 AI agent 协作开发的范式，并尝试实现有助于此目的的 CLI 工具。
