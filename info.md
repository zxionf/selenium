# design CLI

A C++20 command-line tool for managing `.design/` and `.agent/` directories, supporting the minimal closed loop of "change creation → decision append → status query → rule check → fold preview".

## Build

```bash
# Requires: CMake ≥ 3.20, C++20 compiler
cmake -B build -S .
cmake --build build
```

The binary will be at `build/design` (or `build/build/design.exe` on Windows).

Dependencies (CLI11, nlohmann/json) are fetched automatically via CMake FetchContent and statically linked.

## Commands

All commands support:
- `--root <path>` — Project root (default: current dir, searches upward for `.design/`)
- `--output text|json` — Output format (default: text)

### `design new --slug <name>`

Create a new change with design/ and work/ directories.

```bash
design new --slug my-feature
```

Exit codes: 0=success, 1=invalid slug, 2=ID exists, 4=env error

### `design append-decision --context <c> --decision <d> --consequences <e> [--change-id <id>]`

Append a decision record to `.design/DECISIONS.log`.

```bash
design append-decision --context "Choose C++20" --decision "Use modules" --consequences "Cleaner imports" --change-id 20261018-my-feature
```

Exit codes: 0=success, 1=missing params, 4=env error

### `design status [--output json]`

Show current active change status (read-only, fast).

```bash
design status --output json
```

Exit codes: 0=success, 4=env error

### `design lint [--fix]`

Run mechanical rule checks:
1. ID bidirectional consistency
2. DECISIONS.log append-only (via git diff)
3. SPEC.md process words (TODO, WIP, 待办, 下一步, 进行中)
4. Directory integrity (design.md, tasks.md exist)

```bash
design lint
design lint --fix  # auto-fix missing template files
```

Exit codes: 0=all pass, 3=any check fails, 4=env error

### `design close <id> --dry-run`

Preview closing a change (MVP: only `--dry-run` implemented).

```bash
design close 20261018-my-feature --dry-run
```

Exit codes: 0=success, 1=missing --dry-run, 2=ID not found, 4=env error

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | Usage error (missing/invalid params) |
| 2 | State conflict (ID exists, ID not found) |
| 3 | Lint check failed |
| 4 | Environment error (dir not found, git unavailable, permission) |

## JSON Output

Every command supports `--output json` with stable field names. Output is parsable by `jq`.

Example:
```json
{"id":"20261018-my-feature","design_path":".design/changes/.../design.md","work_path":".agent/work/.../tasks.md","state_updated":true}
```

## Testing

```bash
ctest --output-on-failure
```

Runs 10 test cases covering happy paths and error conditions.

## Project Structure

```
.design/
├── INDEX.md
├── SPEC.md
├── DECISIONS.log
└── changes/
    └── <id>/
        └── design.md
.agent/
├── INDEX.md
├── CONTRACT.md
├── STATE.md
└── work/
    └── <id>/
        └── tasks.md
```

ID format: `<YYYYMMDD>-<slug>` (e.g., `20261018-my-feature`)

## Decisions

See `.design/DECISIONS.log` for implementation decisions.