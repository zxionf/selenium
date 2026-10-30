# AGENTS.md

`sele`：管理 `.sele/` 四层任务体系的 C++20 CLI（Plan → Line → Task → Step）。当前版本 **0.20.0**，不兼容 0.12 及更早版本。

## 入口

1. **agent 契约：`.sele/CONTRACT.md`（不可违反清单，优先阅读）**。
2. 实现与命令：`docs/sele-0.20.0.md`。
3. 接口规范：`docs/sele-f.md`；概念语义：`docs/sele-f-d.md`。
4. 当前状态：`sele status --output json`、`sele next --output json`。
5. `.sele/` 是工具私有目录，只通过 `sele` 命令读写；不要用编辑器直接修改。

## 体系最小闭环

```bash
sele init

sele plan new <plan> --goal "..." --acceptance "..." --lines <line>
sele plan submit <plan>
sele plan approve <plan> --by human:<name>

sele line new <line> --plan <plan> --goal "..." --acceptance "..." --modules a,b

sele task add "<title>" --line <line> --acceptance "..."
sele task start T1 --line <line>
sele step add T1 "<title>" --line <line>
sele step start T1 S1 --line <line>
sele step done T1 S1 --line <line>

sele task verify T1 A1 --line <line> --by human:<name>
sele task done T1 --line <line>
sele line verify <line> A1 --plan <plan> --by human:<name>
sele line done <line> --plan <plan>
sele plan verify <plan> PA1 --by human:<name>
sele plan done <plan>
```

## 规则

1. 状态迁移必须由显式命令驱动；禁止手改 `.sele/` 下的 JSON。
2. `log.jsonl` 是 append-only 的审计记录，不要修改或删除。
3. I1：全局至多一个 `in_progress` task；`task start` 会自动暂停当前 task。
4. I2：单 task 内至多一个 `in_progress` step。
5. `task done` 要求所有 step 为 `done`/`skipped`；验收未满足 `review_policy` 时转 `pending_review`。
6. `line done` 要求所有 task 结束且线级验收通过；`plan done` 同理。
7. 提交代码：M2 完成后统一用 `sele git commit --task <id> --step <id>`；在此之前可直接 `git commit`。
8. 验证：`bash tests/sele_v2_tests.sh build/build/sele` 与 `ctest --test-dir build` 必须通过。

## 常用查询

```bash
sele next                 # 就绪任务（拓扑序）
sele status               # 当前计划/线/任务摘要
sele tree                 # 四层结构
sele log --limit 20       # 最近事件
sele check                # 一致性检查
```
