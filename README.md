# selenium

> [!NOTE]
> 不允许ai修改此文件

`sele` 是一个面向 AI agent 协作的 C++20 命令行工具，用「线（line）→ 任务（task）→ 验收（acceptance）→ 合并（merge）」组织开发工作。

它把「什么算完成、什么时候提交、失败怎么回滚」编码成命令，让人类和 agent 遵循同一套规则；所有状态存放在工具私有目录 `.sele/`（类比 `.git/`），只能通过 `sele` 命令读写。

## 特性

- **一条线一个目标**：goal + 验收标准 + 独立 git 分支。
- **task 即提交**：`task checkpoint` 是唯一的自动代码提交点。
- **验收只记账**：`acceptance pass/fail`，工具不替人执行命令。
- **多线并行**：独立分支 + 可选 worktree，会话指针互不干扰。
- **线内并行**：显式 `--group` 并行组与 join 点。
- **完整生命周期**：open → checkpoint → done → merge → archive，另有 revert / rebase。
- **可审计**：append-only 日志，`fsck` 校验 `.sele` 一致性。
- **可脚本化**：所有命令支持 `--output json`，退出码固定。

## 安装

依赖：C++20 编译器、CMake 3.20+；CLI11 与 nlohmann/json 由 CMake FetchContent 自动拉取。

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
# 项目需要 git 和至少一个提交
git init -b main
git add -A && git commit -m "init"

# 初始化 .sele/
sele init

# 建线（draft）：一个目标 + 验收 + 模块
sele line new fix-login \
  --goal "登录失败时给出可操作错误" \
  --acceptance "错误密码返回 401::pytest tests/test_login.py::test_wrong_pwd" \
  --modules auth --type fix

# 挂任务
sele task add "复现 401 分支" --modules auth
sele task add "实现错误提示" --modules auth

# 开线：冻结验收、创建 sele/<id> 分支
sele line open <id>

# 推进任务
sele task start T1
# ... 修改代码 ...
sele task checkpoint T1
sele task done T1

# 记账验收
sele acceptance pass A1 --evidence "pytest 12 passed"

# 收线、合并、归档
sele line done <id>
sele line merge <id> --confirm
sele line archive <id>
```

## 核心概念

| 概念 | 含义 |
|---|---|
| line | 一个带验收的目标，对应分支 `sele/<id>` |
| task | 线上的工作单元，完成时产生一次 checkpoint |
| checkpoint | 一次 git commit（代码 + `.sele` 元数据） |
| acceptance | 线级验收项，由人或 agent 记账 |
| session | 当前会话指向的线，多 agent 用 `--session` 隔离 |
| worktree | 把一条线检出到独立目录，用于并行写代码 |
| merged / archived | 已合并 / 已归档，只读 |

## 命令

```text
sele init                     初始化 .sele/
sele line new|list|show|check|open|switch|done|merge|archive|...
sele task add|list|start|checkpoint|done|fail|retry|block|unblock|revert
sele acceptance pass|fail     记账验收
sele overview                 全局概览
sele log [--events]           审计日志
sele fsck                     一致性检查
sele worktree add|list|remove 多线工作树
sele version                  版本信息
```

每个命令都支持 `--root <dir>`、`--output text|json`、`--session <id>`；完整参数见 `sele <命令> --help`。

## 配置

配置文件为 `.sele/config.json`，可控制：

- 提交信息的语言（英文 / 中文）与 scope；
- 模块白名单；
- checkpoint 策略；
- 操作者身份来源。

详见 [`docs/sele-config.md`](docs/sele-config.md)。

## 文档

| 文档 | 内容 |
|---|---|
| [`docs/sele-user-guide.md`](docs/sele-user-guide.md) | 人类使用指南：安装、工作流、命令速查、FAQ |
| [`docs/sele-config.md`](docs/sele-config.md) | 配置字段、环境变量、提交信息规则 |
| [`docs/sele-v1-spec.md`](docs/sele-v1-spec.md) | 实现规格：状态机、数据模型、并发、审计 |
| [`docs/sele-git.md`](docs/sele-git.md) | 与 git 的协作：提交时刻表、干净判定、冲突 |
| [`docs/README.md`](docs/README.md) | 文档索引（含面向 agent 的操作手册入口） |

## 开发

```bash
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
```

- 源码：`src/cli/`（子命令）、`src/core/`（领域逻辑）、`src/util/`（输出与退出码）。
- 测试：`tests/sele_tests.sh`（端到端，基于临时 git 仓库）。
- 构建开启 `-Wall -Wextra -Wpedantic -Werror`，警告即错误。

本仓库用于探索人类与ai-agent协作开发的范式，并尝试实现有助于此目的的cli工具。
