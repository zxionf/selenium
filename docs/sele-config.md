# sele 配置文档

> 适用版本：0.11.1。配置文件：`.sele/config.json`。

---

## 1. 文件位置与编辑方式

- 路径：`.sele/config.json`，由 `sele init` 生成。
- 它是 **tracked** 文件（随仓库提交），可以手工编辑；不要手改 `.sele/runtime/`。
- 每条命令启动时读取配置，改完立即生效；可用 `sele overview --output json` 验证。
- 文件必须是合法 JSON；字段缺失时使用默认值，非法值会让命令退 4。

## 2. 完整示例

```json
{
  "schema_version": 1,
  "actor": "git",
  "modules": { "allow": [] },
  "checkpoint": { "strategy": "git-commit", "allow_empty": false },
  "commit": { "language": "en", "scope": "sele", "task_type": "auto" }
}
```

## 3. 字段总览

| 字段 | 类型 | 默认 | 作用 |
|---|---|---|---|
| `schema_version` | int | `1` | 配置格式版本；只认 1 |
| `actor` | string | `"git"` | 审计与验收的操作者来源 |
| `modules.allow` | string[] | `[]` | 模块白名单；空表示不限制 |
| `checkpoint.strategy` | string | `"git-commit"` | checkpoint 策略；目前只支持 git-commit |
| `checkpoint.allow_empty` | bool | `false` | 保留字段，当前未生效（见 §4.4） |
| `commit.language` | string | `"en"` | subject 语言：`en` / `zh` |
| `commit.scope` | string | `"sele"` | 提交前缀 scope：`sele` / `module` / 字面量 |
| `commit.task_type` | string | `"auto"` | task 提交类型：`auto` / `chore` / `feat` / `fix` / `refactor` |

## 4. 字段详解

### 4.1 `schema_version`

固定 `1`。读到其他值直接退 4：

```
Unsupported .sele schema_version: 2
```

### 4.2 `actor`

决定审计日志、`line.created_by`、`task.created_by`、`acceptance.by` 的值。

| 配置值 | 行为 |
|---|---|
| `"git"`（默认） | 读取 `git config user.name`；读不到用 `unknown` |
| 其他字符串 | 直接使用该字符串 |

优先级：`SELE_ACTOR` 环境变量 > `config.actor`。

### 4.3 `modules.allow`

- `[]`：不限制，`line new --modules` 接受任意合法标签。
- 非空：`line new` 的每个 module 必须已登记；`line check` 的 `module_registry` 也会校验。

示例：

```json
"modules": { "allow": ["auth", "ui", "db", "docs"] }
```

### 4.4 `checkpoint`

| 字段 | 说明 |
|---|---|
| `strategy` | 目前只认 `"git-commit"`；其他值退 4 |
| `allow_empty` | **保留字段**：当前实现不创建空提交；`task checkpoint` 无改动时复用上一个 checkpoint（`changed:false`）。保留是为了未来支持空提交 |

### 4.5 `commit.language`

- 取值：`"en"` / `"zh"`，默认 `"en"`。
- 只影响提交信息里**模板固定的词**；id、type、scope 不翻译。
- 环境变量 `SELE_COMMIT_LANG` 可临时覆盖。
- `--msg` 完全覆盖，不受此配置影响。

### 4.6 `commit.scope`

| 取值 | 效果 |
|---|---|
| `"sele"`（默认） | 所有自动提交都用 `sele` |
| `"module"` | task checkpoint 用 `task.modules[0]`；line 级提交（plan/done/merge/rebase）仍用 `sele` |
| 其他字符串 | 所有自动提交都用该字符串 |

示例：`scope: "module"` 时，`task add ... --modules auth` 的 checkpoint 提交为 `feat(auth): T1 ...`。

### 4.7 `commit.task_type`

| 取值 | 效果 |
|---|---|
| `"auto"`（默认） | 按 line.type 映射：feature→feat、fix→fix、refactor→refactor、chore/spike→chore |
| `"chore"` / `"feat"` / `"fix"` / `"refactor"` | 所有 task checkpoint 固定用该类型 |

非 task 提交（plan/done/merge/rebase）始终用 `chore`。

## 5. 环境变量与优先级

| 变量 | 作用 | 优先级 |
|---|---|---|
| `SELE_ACTOR` | 覆盖操作者 | 最高 |
| `SELE_COMMIT_LANG` | 覆盖提交 subject 语言（`en`/`zh`） | 高于 config |
| `SELE_SESSION` | 覆盖会话 id | 低于 `--session`，高于默认 `default` |

其余选项：

| 选项 | 作用 | 默认 |
|---|---|---|
| `--root <dir>` | 项目根 | 当前目录，向上查找 `.sele/` |
| `--output text\|json` | 输出格式 | `text` |
| `--session <id>` | 会话指针 | `SELE_SESSION` 或 `default` |

## 6. 提交信息规则

所有自动提交的格式：

```
<type>(<scope>): <subject>
```

| 提交点 | en | zh |
|---|---|---|
| plan | `chore(sele): plan <id>` | `chore(sele): 规划 <id>` |
| task checkpoint | `<task type>(<scope>): T1 <title>` | 同左（title 原样） |
| line done | `chore(sele): line <id> done` | `chore(sele): 完成线 <id>` |
| mark merged | `chore(sele): mark <id> merged` | `chore(sele): 标记 <id> 为已合并` |
| unmark | `chore(sele): unmark <id> merged` | `chore(sele): 撤销 <id> 的已合并标记` |
| merge commit | `chore(sele): merge <id>` | `chore(sele): 合并 <id>` |
| record merge | `chore(sele): record merge <id>` | `chore(sele): 记录 <id> 的合并提交` |
| rebase | `chore(sele): rebase <id>` | `chore(sele): 变基 <id>` |

`--msg` 时的提交信息完全等于你给的内容，不拼前缀。

## 7. 示例配置

### 7.1 默认（英文、固定 scope）

```json
"commit": { "language": "en", "scope": "sele", "task_type": "auto" }
```

### 7.2 中文 subject、按模块 scope（当前仓库）

```json
"commit": { "language": "zh", "scope": "module", "task_type": "auto" }
```

效果：`chore(docs): T1 编写文档`、`chore(sele): 规划 <id>`。

### 7.3 固定 scope（例如所有提交都进 `ui`）

```json
"commit": { "language": "en", "scope": "ui", "task_type": "auto" }
```

效果：`feat(ui): T1 ...`、`chore(ui): plan <id>`。

### 7.4 模块白名单

```json
"modules": { "allow": ["auth", "ui", "docs"] }
```

未登记模块时 `line new` 退 1，`line check` 的 `module_registry` 失败。

## 8. 校验与错误

| 情况 | 结果 |
|---|---|
| `schema_version != 1` | 退 4 |
| `checkpoint.strategy != "git-commit"` | 退 4 |
| `commit.language` 不是 `en`/`zh` | 退 4 |
| `commit.scope` 为空字符串 | 退 4 |
| `commit.task_type` 不在枚举内 | 退 4 |
| 字段缺失 | 使用默认值 |
| JSON 非法 | 退 4 |

错误信息会指出具体字段，例如：

```
Unsupported commit.language: ja
```

## 9. 迁移与兼容

- **旧配置（0.10 及以前）没有 `commit` 段**：自动使用默认 `en` / `sele` / `auto`，无需迁移。
- `sele init` 在新项目会写入完整配置；已有 `.sele/` 时进入 repair 模式，**只补缺失文件，不修改已有 config 内容**。也就是说，旧 config 不会自动出现 `commit` 段，需要手工添加。
- 0.11 起移除了 `--legacy` 与 `.design/.agent` 支持，配置格式本身不变。
- 配置是 tracked 文件；手工修改后应提交，团队才能共享同一套规则。

## 10. 常见问题

**Q：改了配置但提交信息没变？**
确认命令读取的是同一个 `.sele/config.json`（`--root` 是否指对）；`--msg` 会覆盖所有配置。

**Q：前缀能改成中文吗？**
不能。`type(scope):` 固定英文，这是为了让 conventional commit 工具与 CI 能解析。

**Q：能按 line 覆盖语言吗？**
目前不支持，配置是全局的。需要不同语言就改 config，或对单次提交用 `--msg`。

**Q：id 能用中文吗？**
不能。id 是 `<UTC日期>-<slug>`，slug 只允许 `[a-z0-9-]`。

**Q：`allow_empty` 设为 true 会创建空提交吗？**
不会。当前实现不创建空提交；该字段保留但未生效。

**Q：`actor` 用 `git` 但仓库没配 user.name？**
会退化为 `unknown`；建议 `git config user.name "Your Name"` 或设置 `SELE_ACTOR`。
