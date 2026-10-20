# Spec

## 目标
<!-- 只写稳定的规格，不写过程性内容 -->

## 约束

## Context
工具面向 agent 调用，默认输出格式是 `text`。此前六个子命令只渲染 JSON，默认调用静默退出 0（`sele status` 看起来"没有输出"）；`sele close` 也只有 `--dry-run` 预览，无法完成 `design-self.md` 定义的"变更合并"，且预览把 design.md 读入后未使用、小节标题写死，未匹配时用 SPEC 第一个小节兜底。两处缺陷均已修复，本节起描述修复后的当前事实。

## 变更内容
1. **输出**：六个子命令各构造一份 JSON，再由同一份数据渲染 text，用 `Output::print_both(text, json)` 择一输出。text 一律走 stdout，错误仍走 stderr。
2. **各命令 text 形态**：
   - `status`：四行摘要，无活跃变更时首行为 `Active change: none`。
   - `new` / `init`：首行摘要加缩进明细（design / work / agent 路径）。
   - `append-decision`：`Appended decision #<n> -> <path>` 单行。
   - `lint`：首行 `Lint passed` / `Lint failed`，随后每项 `[ok]` / `[fail]` / `[skip]`。
   - `close`：预览输出 `Close preview for <id> ...` 加折叠计划；实际关闭输出 `Closed <id> (spec folded, directories kept)`。

   ```
   Active change: 20261020-my-feature
   Tasks: 2/5 done
   Decisions: 3
   Consistent: yes
   ```

3. **`close` 折叠语义**：解析 `design.md` 的 `##` 小节，与 `SPEC.md` 小节按"标题互为子串"匹配；命中则替换该小节正文（修订式，不追加补丁段），未命中则在 `SPEC.md` 末尾追加同名小节；正文为空或只含 `<!-- -->` 占位的小节跳过，不用模板占位覆盖既有规格。
4. **目录与指针**：`.design/changes/<id>/` 与 `.agent/work/<id>/` 原地保留，历史交给 git；`STATE.md` 仅当 `Active Change:` 正是被关闭 id 时才复位为 `无`，指向其他变更时保持不变。
5. **`--dry-run`**：只输出预览，不写任何文件；列出真实解析出的 design 小节与折叠计划（`replace` / `append` / `skipped`），未匹配的 `suggested_mapping` 值为空字符串。

## 影响面
- 代码集中在 `src/cli/*.cpp`；`util::Output` 接口未改动（`print_both` 早已存在）。`close.cpp` 内部新增分节解析、渲染、指针读写辅助函数。
- 对外契约：JSON 字段名、退出码、参数、文件布局不变。两处取值变化：`suggested_mapping` 的未匹配值由"SPEC 第一个小节"变为空串；`sele close <id>` 由"报错退出"变为"执行折叠"。
- 写入面扩展：`close` 会写 `SPEC.md`，并在指针命中时写 `STATE.md`；`--dry-run` 不写任何文件。
- 测试：14 个用例覆盖 JSON 契约、text 非空、折叠写入、指针保护、未匹配不捏造映射、嵌套缩进存活。
- `SPEC.md` 的分节沿用模板四维（`Context/变更内容/影响面/兼容性与迁移`），由 close 自动折叠维护。

## 兼容性与迁移
- 不破坏既有 JSON 消费路径；依赖"`close` 报错"的调用方改为显式传 `--dry-run`。
- 无需数据迁移；`changes/<id>` 与 `work/<id>` 原地保留，git 保留历史。
- 折叠按标题匹配，因此不同变更若使用同名小节（模板四维即是）会互相覆盖；跨变更的结论需要先合并成一份终稿再关闭。若将来改为按"概念"分节，需要一次显式的 SPEC 重构变更。
