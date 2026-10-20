# 20261020-status-text-output

## Context

`--output text` 是默认输出格式（`info.md` 与 `add_output_opt` 均如此声明），但六个子命令的 callback 都只调用了 `output.print_json(...)`：

- `init`、`new`、`append-decision`、`status`、`lint`、`close` 在 text 模式下不打印任何内容，退出码却是 0。
- 直接后果是 `sele status` 看起来"没有输出"，人无法判断当前活跃变更；`sele lint` 失败时也只有退出码 3，看不到是哪一项没过。
- JSON 路径本身正常，所以现有测试（只断言 `--output json`）全部通过，掩盖了这个缺陷。

## 变更内容

1. 统一按"先算结构、再渲染文本"的形态输出：命令仍构造原有 JSON 对象（字段名不变），另外由同一份数据渲染一段文本，用 `Output::print_both(text, json)` 按格式择一输出。
2. 各命令的文本形态定为：

   - `status`：
     ```
     Active change: 20261020-status-text-output
     Tasks: 0/2 done
     Decisions: 0
     Consistent: yes
     ```
     无活跃变更时首行为 `Active change: none`；`consistent` 为 `yes`/`no`。
   - `new`：`Created <id>` 加三行缩进明细（design / work / state）。
   - `init`：`Initialized .design/ and .agent/ in <root>` 加 design / agent 两行明细。
   - `append-decision`：`Appended decision #<index> -> <path>` 单行。
   - `lint`：首行 `Lint passed` / `Lint failed`，随后每个检查一行 `[ok]` / `[fail] <name>`。
   - `close`：`Close preview for <id> (dry run, nothing written)`，随后列出 design 小节、SPEC 小节与建议映射。
3. 文本一律输出到 stdout，错误仍走 `Output::error`（stderr），保持"成功=stdout、失败=stderr"的既有约定。
4. 文本文案保持英文，与现有 CLI 错误信息一致；JSON 字段名一个都不改。

## 影响面

- 仅影响 `src/cli/*.cpp` 的渲染分支，`util::Output` 接口已经够用（`print_both` 早已存在但无人调用），不改 `output.hpp/.cpp`。
- 对外契约：JSON 输出、退出码、参数、文件布局全部不变；变化只发生在原本为空的 text 输出上，属纯增量。
- 测试：`tests/cli_tests.ps1` / `cli_tests.sh` 增加 text 模式断言（默认格式下必须非空且含关键字段），用例数由 10 增至 12。
- 文档：`info.md` 补一节文本输出示例。

## 兼容性与迁移

- 不破坏兼容：依赖 stdout 为空的脚本本就无法获得信息，新增文本不影响 `--output json` 的机器消费路径。
- 无需数据迁移，无需改动 `.design/`、`.agent/` 的既有文件格式。
- 如果将来需要机器可解析的 text 形态，再单独引入 `--output plain` 之类的第三格式，本次不做。
