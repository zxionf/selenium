#!/usr/bin/env bash
# sele 0.20.0 end-to-end tests.
# Usage: bash tests/sele_v2_tests.sh [path-to-sele]
set -uo pipefail

BIN=${1:-build/build/sele}
if [ ! -x "$BIN" ]; then
    echo "sele binary not found: $BIN" >&2
    exit 1
fi
BIN=$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")

PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); printf 'PASS: %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf 'FAIL: %s\n' "$1"; }

expect_code() {
    local want=$1
    shift
    local got=0
    "$@" >/dev/null 2>&1 || got=$?
    [ "$got" -eq "$want" ]
}

setup_project() {
    ROOT=$(mktemp -d)
    "$BIN" init --root "$ROOT" >/dev/null
    "$BIN" plan new p --root "$ROOT" --goal "goal" --acceptance "plan ok" --lines a >/dev/null
    "$BIN" plan submit p --root "$ROOT" >/dev/null
    "$BIN" plan approve p --root "$ROOT" --by human:alice >/dev/null
    "$BIN" line new a --root "$ROOT" --plan p --goal "line goal" --acceptance "line ok" \
        --modules core >/dev/null
}

setup_git_project() {
    ROOT=$(mktemp -d)
    git -C "$ROOT" init -q -b main
    git -C "$ROOT" config user.email t@t
    git -C "$ROOT" config user.name tester
    echo "a" > "$ROOT/a.txt"
    git -C "$ROOT" add -A && git -C "$ROOT" commit -qm init
    "$BIN" init --root "$ROOT" >/dev/null
    "$BIN" plan new p --root "$ROOT" --goal "g" --acceptance "pa" --lines a >/dev/null
    "$BIN" plan submit p --root "$ROOT" >/dev/null
    "$BIN" plan approve p --root "$ROOT" --by human:alice >/dev/null
    "$BIN" line new a --root "$ROOT" --plan p --goal "ga" --acceptance "la" --modules core >/dev/null
    "$BIN" task add "t" --root "$ROOT" --line a --acceptance "ok" >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "s" --root "$ROOT" --line a >/dev/null
    "$BIN" step start T1 S1 --root "$ROOT" --line a >/dev/null
}

test_git_commit() {
    setup_git_project
    echo "b" > "$ROOT/b.txt"
    if "$BIN" git commit --root "$ROOT" --task T1 --step S1 --message "feat: add b" \
        --files b.txt --output json | jq -e '.commit | length > 0' >/dev/null; then
        pass "git commit records the commit"
    else
        fail "git commit records the commit"
    fi
    if "$BIN" log --root "$ROOT" --output json |
        jq -e '[.items[] | select(.op == "git.commit")] | length == 1' >/dev/null; then
        pass "git commit writes an event"
    else
        fail "git commit writes an event"
    fi
    rm -rf "$ROOT"
}

test_git_drift_d2() {
    setup_git_project
    echo "b" > "$ROOT/b.txt"
    "$BIN" git commit --root "$ROOT" --task T1 --step S1 --message "feat: add b" \
        --files b.txt >/dev/null
    "$BIN" status --root "$ROOT" >/dev/null
    git -C "$ROOT" reset --hard HEAD~1 >/dev/null
    if expect_code 4 "$BIN" git commit --root "$ROOT" --task T1 --step S1 --message "x"; then
        pass "git commit is blocked after HEAD rewind (D2)"
    else
        fail "git commit is blocked after HEAD rewind (D2)"
    fi
    rm -rf "$ROOT"
}

test_plan_archive_restore() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" task done T1 --root "$ROOT" --line a >/dev/null
    "$BIN" line verify a A1 --root "$ROOT" --plan p --by human:alice >/dev/null
    "$BIN" line done a --root "$ROOT" --plan p >/dev/null
    "$BIN" plan verify p PA1 --root "$ROOT" --by human:alice >/dev/null
    "$BIN" plan done p --root "$ROOT" >/dev/null
    if "$BIN" plan archive p --root "$ROOT" --output json | jq -e '.files >= 1' >/dev/null; then
        pass "plan archive moves files"
    else
        fail "plan archive moves files"
    fi
    if ls "$ROOT/.sele/archive/"*/_manifest.json >/dev/null 2>&1; then
        pass "archive manifest is written"
    else
        fail "archive manifest is written"
    fi
    "$BIN" plan restore p --root "$ROOT" >/dev/null
    if "$BIN" plan show p --root "$ROOT" --output json | jq -e '.status == "active"' >/dev/null; then
        pass "plan restore brings files back"
    else
        fail "plan restore brings files back"
    fi
    rm -rf "$ROOT"
}

test_cache_index() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    if "$BIN" cache rebuild --root "$ROOT" --output json | jq -e '.tasks == 1' >/dev/null; then
        pass "cache rebuild writes the index"
    else
        fail "cache rebuild writes the index"
    fi
    if "$BIN" task locate T1 --root "$ROOT" --line a --output json |
        jq -e '.file | startswith("lines/")' >/dev/null; then
        pass "task locate returns the task file"
    else
        fail "task locate returns the task file"
    fi
    rm -rf "$ROOT"
}

test_log_compact() {
    setup_project
    if "$BIN" log compact --root "$ROOT" --output json |
        jq -e '.command == "log.compact"' >/dev/null; then
        pass "log compact archives the event log"
    else
        fail "log compact archives the event log"
    fi
    if ls "$ROOT/.sele/archive/"log-*.jsonl >/dev/null 2>&1; then
        pass "log compact writes an archive file"
    else
        fail "log compact writes an archive file"
    fi
    rm -rf "$ROOT"
}

test_decision_replay() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    "$BIN" decision record "选 JWT 而非 session" --root "$ROOT" --plan p --task T1 >/dev/null
    if "$BIN" log --decisions --root "$ROOT" --output json |
        jq -e '.items | length == 1' >/dev/null; then
        pass "decision record writes a decision event"
    else
        fail "decision record writes a decision event"
    fi
    if "$BIN" log --replay --root "$ROOT" --output json |
        jq -e '.items | length >= 1' >/dev/null; then
        pass "log --replay prints the timeline"
    else
        fail "log --replay prints the timeline"
    fi
    rm -rf "$ROOT"
}

test_task_insert_move() {
    setup_project
    "$BIN" line new b --root "$ROOT" --plan p --goal "gb" --acceptance "lb" --modules core >/dev/null
    "$BIN" task add "a1" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "b1" --root "$ROOT" --line b >/dev/null
    "$BIN" task insert "a0" --root "$ROOT" --line a --after T1 >/dev/null
    if "$BIN" task list --root "$ROOT" --line a --output json |
        jq -e '[.items[] | select(.id == "T2")] | length == 1' >/dev/null; then
        pass "task insert adds a task after an anchor"
    else
        fail "task insert adds a task after an anchor"
    fi
    "$BIN" task move T2 --root "$ROOT" --to-line b >/dev/null
    if "$BIN" task list --root "$ROOT" --line b --output json |
        jq -e '[.items[] | select(.id == "T2")] | length == 1' >/dev/null; then
        pass "task move changes the owning line"
    else
        fail "task move changes the owning line"
    fi
    rm -rf "$ROOT"
}

test_cross_line_dep() {
    setup_project
    "$BIN" line new b --root "$ROOT" --plan p --goal "gb" --acceptance "lb" --modules core >/dev/null
    "$BIN" task add "a1" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "b1" --root "$ROOT" --line b >/dev/null
    "$BIN" task add "a2" --root "$ROOT" --line a --depends b.T1 >/dev/null
    if expect_code 2 "$BIN" task start T2 --root "$ROOT" --line a; then
        pass "cross-line dependency blocks task start"
    else
        fail "cross-line dependency blocks task start"
    fi
    "$BIN" task start T1 --root "$ROOT" --line b >/dev/null
    "$BIN" task done T1 --root "$ROOT" --line b >/dev/null
    if "$BIN" task start T2 --root "$ROOT" --line a >/dev/null 2>&1; then
        pass "cross-line dependency unblocks after done"
    else
        fail "cross-line dependency unblocks after done"
    fi
    rm -rf "$ROOT"
}

test_pause_snapshot() {
    setup_git_project
    "$BIN" task pause T1 --root "$ROOT" --line a --reason "switch" >/dev/null
    if jq -e '.tasks[] | select(.pause_context.git_snapshot.head != null)' \
        "$ROOT/.sele/lines/p.a.json" >/dev/null 2>&1; then
        pass "pause records a git snapshot"
    else
        fail "pause records a git snapshot"
    fi
    rm -rf "$ROOT"
}

test_skipped_count() {
    setup_project
    "$BIN" task add "t1" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "t2" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" task start T2 --root "$ROOT" --line a >/dev/null
    if "$BIN" task list --root "$ROOT" --line a --output json |
        jq -e '[.items[] | select(.id == "T1")][0].skipped_count == 1' >/dev/null; then
        pass "switching away increments skipped_count"
    else
        fail "switching away increments skipped_count"
    fi
    "$BIN" task keep T1 --root "$ROOT" --line a >/dev/null
    if "$BIN" task list --root "$ROOT" --line a --output json |
        jq -e '[.items[] | select(.id == "T1")][0].skipped_count == 0' >/dev/null; then
        pass "task keep resets skipped_count"
    else
        fail "task keep resets skipped_count"
    fi
    rm -rf "$ROOT"
}

test_step_lock() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "s" --root "$ROOT" --line a >/dev/null
    "$BIN" step lock T1 S1 --root "$ROOT" --line a --reason "review" >/dev/null
    if expect_code 2 "$BIN" step start T1 S1 --root "$ROOT" --line a; then
        pass "locked step rejects start"
    else
        fail "locked step rejects start"
    fi
    "$BIN" step unlock T1 S1 --root "$ROOT" --line a >/dev/null
    if "$BIN" step start T1 S1 --root "$ROOT" --line a >/dev/null 2>&1; then
        pass "unlocked step can start"
    else
        fail "unlocked step can start"
    fi
    rm -rf "$ROOT"
}

test_step_update_delete_move() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "s1" --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "s2" --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "s3" --root "$ROOT" --line a >/dev/null
    "$BIN" step update T1 S1 --title "design" --root "$ROOT" --line a >/dev/null
    "$BIN" step move T1 S3 --after S1 --root "$ROOT" --line a >/dev/null
    "$BIN" step delete T1 S2 --reason "not needed" --root "$ROOT" --line a >/dev/null
    if "$BIN" step list T1 --root "$ROOT" --line a --output json |
        jq -e '(.items | map(.id)) == ["S1", "S3"] and .items[0].title == "design"' >/dev/null; then
        pass "step update/move/delete work"
    else
        fail "step update/move/delete work"
    fi
    rm -rf "$ROOT"
}

test_full_ref() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    if "$BIN" task locate a.T1 --root "$ROOT" --output json |
        jq -e '.task == "T1" and .line == "a"' >/dev/null; then
        pass "line.T1 resolves"
    else
        fail "line.T1 resolves"
    fi
    if "$BIN" task locate p.a.T1 --root "$ROOT" --output json |
        jq -e '.plan == "p" and .line == "a"' >/dev/null; then
        pass "plan.line.T1 resolves"
    else
        fail "plan.line.T1 resolves"
    fi
    rm -rf "$ROOT"
}

test_next_i1() {
    setup_project
    "$BIN" task add "t1" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "t2" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    if "$BIN" next --root "$ROOT" --output json | jq -e '.items | length == 0' >/dev/null; then
        pass "next is empty while a task is in_progress (I1)"
    else
        fail "next is empty while a task is in_progress (I1)"
    fi
    "$BIN" task done T1 --root "$ROOT" --line a >/dev/null
    if "$BIN" next --root "$ROOT" --output json | jq -e '.items | length >= 1' >/dev/null; then
        pass "next lists ready tasks after the current task is done"
    else
        fail "next lists ready tasks after the current task is done"
    fi
    rm -rf "$ROOT"
}

test_p1_commands() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a --acceptance "ok" >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" task done T1 --root "$ROOT" --line a >/dev/null
    "$BIN" task reject T1 --root "$ROOT" --line a --reason "redo" >/dev/null
    if "$BIN" task status T1 --root "$ROOT" --line a --output json |
        jq -e '.status == "in_progress"' >/dev/null; then
        pass "task reject returns to in_progress"
    else
        fail "task reject returns to in_progress"
    fi
    "$BIN" task update T1 --root "$ROOT" --line a --modules x,y --review-policy strict >/dev/null
    if "$BIN" task status T1 --root "$ROOT" --line a --output json |
        jq -e '.modules == ["x", "y"] and .review_policy == "strict"' >/dev/null; then
        pass "task update changes modules and review policy"
    else
        fail "task update changes modules and review policy"
    fi
    "$BIN" line block a --root "$ROOT" --plan p --reason "wait" >/dev/null
    "$BIN" line unblock a --root "$ROOT" --plan p >/dev/null
    if "$BIN" line list --root "$ROOT" --plan p --output json |
        jq -e '[.items[] | select(.id == "a")][0].status == "active"' >/dev/null; then
        pass "line block/unblock round-trips"
    else
        fail "line block/unblock round-trips"
    fi
    "$BIN" step add T1 S1 --root "$ROOT" --line a >/dev/null
    "$BIN" step start T1 S1 --root "$ROOT" --line a >/dev/null
    "$BIN" step fail T1 S1 --root "$ROOT" --line a --reason "boom" >/dev/null
    if "$BIN" step list T1 --root "$ROOT" --line a --output json |
        jq -e '[.items[] | select(.id == "S1")][0].status == "failed"' >/dev/null; then
        pass "step fail"
    else
        fail "step fail"
    fi
    "$BIN" step add T1 S2 --root "$ROOT" --line a >/dev/null
    "$BIN" step start T1 S2 --root "$ROOT" --line a >/dev/null
    "$BIN" step block T1 S2 --root "$ROOT" --line a --reason "wait" >/dev/null
    "$BIN" step unblock T1 S2 --root "$ROOT" --line a >/dev/null
    if "$BIN" step list T1 --root "$ROOT" --line a --output json |
        jq -e '[.items[] | select(.id == "S2")][0].status == "in_progress"' >/dev/null; then
        pass "step block/unblock round-trips"
    else
        fail "step block/unblock round-trips"
    fi
    "$BIN" plan abort p --root "$ROOT" --reason "cancel" >/dev/null
    if "$BIN" plan show p --root "$ROOT" --output json | jq -e '.status == "aborted"' >/dev/null; then
        pass "plan abort"
    else
        fail "plan abort"
    fi
    rm -rf "$ROOT"
}

test_depends_uid() {
    setup_project
    "$BIN" line new b --root "$ROOT" --plan p --goal "gb" --acceptance "lb" --modules core >/dev/null
    "$BIN" task add "a1" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "b1" --root "$ROOT" --line b >/dev/null
    "$BIN" task add "a2" --root "$ROOT" --line a --depends b.T1 >/dev/null
    if "$BIN" task status T2 --root "$ROOT" --line a --output json |
        jq -e '.depends[0].uid | startswith("tsk_")' >/dev/null; then
        pass "depends stores uid"
    else
        fail "depends stores uid"
    fi
    local file="$ROOT/.sele/lines/p.a.json"
    jq '(.tasks[] | select(.id == "T2") | .depends) = [{"type":"task","plan":"p","line":"b","id":"T1"}]' \
        "$file" > "$file.tmp" && mv "$file.tmp" "$file"
    if "$BIN" task start T2 --root "$ROOT" --line a >/dev/null 2>&1; then
        fail "legacy depends blocks start"
    else
        pass "legacy depends blocks start"
    fi
    rm -rf "$ROOT"
}

test_check_extended() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    "$BIN" cache rebuild --root "$ROOT" >/dev/null
    jq '.tasks = []' "$ROOT/.sele/lines/p.a.json" > "$ROOT/.sele/lines/p.a.json.tmp" &&
        mv "$ROOT/.sele/lines/p.a.json.tmp" "$ROOT/.sele/lines/p.a.json"
    if expect_code 3 "$BIN" check --root "$ROOT"; then
        pass "check detects stale index"
    else
        fail "check detects stale index"
    fi
    rm -rf "$ROOT"

    setup_project
    mkdir -p "$ROOT/.sele/archive/bogus-1"
    if expect_code 3 "$BIN" check --root "$ROOT"; then
        pass "check detects archive without manifest"
    else
        fail "check detects archive without manifest"
    fi
    rm -rf "$ROOT"

    setup_project
    "$BIN" task add "t1" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "t2" --root "$ROOT" --line a >/dev/null
    local file u1 u2
    file="$ROOT/.sele/lines/p.a.json"
    u1=$(jq -r '.tasks[0].uid' "$file")
    u2=$(jq -r '.tasks[1].uid' "$file")
    jq --arg uid "$u2" '(.tasks[0].depends) = [{"type":"task","uid":$uid}]' "$file" > "$file.tmp" &&
        mv "$file.tmp" "$file"
    jq --arg uid "$u1" '(.tasks[1].depends) = [{"type":"task","uid":$uid}]' "$file" > "$file.tmp" &&
        mv "$file.tmp" "$file"
    if expect_code 3 "$BIN" check --root "$ROOT"; then
        pass "check detects task dependency cycle"
    else
        fail "check detects task dependency cycle"
    fi
    rm -rf "$ROOT"
}

test_query_options() {
    setup_git_project
    echo "b" > "$ROOT/b.txt"
    "$BIN" git commit --task T1 --step S1 --line a --message "c1" --root "$ROOT" >/dev/null
    if [ "$("$BIN" log --since "2999-01-01T00:00:00Z" --root "$ROOT" --output json | jq '.items | length')" = "0" ]; then
        pass "log --since filters future events"
    else
        fail "log --since filters future events"
    fi
    if "$BIN" log --step S1 --root "$ROOT" --output json | jq -e '.items | length >= 1' >/dev/null; then
        pass "log --step filters by step"
    else
        fail "log --step filters by step"
    fi
    if "$BIN" status --with-git --root "$ROOT" --output json | jq -e '.git.head != null' >/dev/null; then
        pass "status --with-git includes git"
    else
        fail "status --with-git includes git"
    fi
    "$BIN" step done T1 S1 --root "$ROOT" --line a >/dev/null
    "$BIN" task approve T1 --root "$ROOT" --line a >/dev/null
    "$BIN" task archive T1 --root "$ROOT" --line a >/dev/null
    if [ "$("$BIN" task list --root "$ROOT" --line a --output json | jq '.items | length')" = "0" ] &&
        [ "$("$BIN" task list --root "$ROOT" --line a --all --output json | jq '.items | length')" = "1" ]; then
        pass "task list hides archived unless --all"
    else
        fail "task list hides archived unless --all"
    fi
    rm -rf "$ROOT"
}

test_config() {
    setup_git_project
    if "$BIN" config --root "$ROOT" --output json |
        jq -e '.module_scope == "warn"' >/dev/null; then
        pass "config show returns defaults"
    else
        fail "config show returns defaults"
    fi
    jq '.module_scope="block"' "$ROOT/.sele/config.json" > "$ROOT/.sele/config.json.tmp" &&
        mv "$ROOT/.sele/config.json.tmp" "$ROOT/.sele/config.json"
    echo "b" > "$ROOT/b.txt"
    if expect_code 2 "$BIN" git commit --task T1 --step S1 --line a --message x --files b.txt --root "$ROOT"; then
        pass "module_scope block rejects out-of-scope files"
    else
        fail "module_scope block rejects out-of-scope files"
    fi
    jq '.module_scope="warn" | .message_convention="sele"' "$ROOT/.sele/config.json" > "$ROOT/.sele/config.json.tmp" &&
        mv "$ROOT/.sele/config.json.tmp" "$ROOT/.sele/config.json"
    "$BIN" git commit --task T1 --step S1 --line a --message c1 --root "$ROOT" >/dev/null
    if git -C "$ROOT" log -1 --pretty=%s | grep -q '^sele(T1/S1):'; then
        pass "message_convention sele prefixes the commit"
    else
        fail "message_convention sele prefixes the commit"
    fi
    rm -rf "$ROOT"
}

test_git_helpers() {
    setup_git_project
    git -C "$ROOT" add -A && git -C "$ROOT" commit -qm state
    echo "change" >> "$ROOT/a.txt"
    if "$BIN" git stash --task T1 --root "$ROOT" >/dev/null 2>&1; then
        pass "git stash --task works"
    else
        fail "git stash --task works"
    fi
    if "$BIN" git stash pop --root "$ROOT" >/dev/null 2>&1; then
        pass "git stash pop works"
    else
        fail "git stash pop works"
    fi
    "$BIN" git branch create dev --root "$ROOT" >/dev/null
    "$BIN" git branch switch main --root "$ROOT" >/dev/null
    if "$BIN" git health --root "$ROOT" --output json |
        jq -e '.git.branch == "main"' >/dev/null; then
        pass "git branch/health works"
    else
        fail "git branch/health works"
    fi
    rm -rf "$ROOT"
}

test_agent_prompt() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    if "$BIN" agent --root "$ROOT" --plan p --line a --output json |
        jq -e '.prompt | contains("T1")' >/dev/null; then
        pass "agent prompt includes the current task"
    else
        fail "agent prompt includes the current task"
    fi
    rm -rf "$ROOT"
}

test_list_text() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    if "$BIN" plan list --root "$ROOT" | grep -q 'p  goal'; then
        pass "plan list text includes entries"
    else
        fail "plan list text includes entries"
    fi
    if "$BIN" line list --root "$ROOT" --plan p | grep -q 'p.a'; then
        pass "line list text includes entries"
    else
        fail "line list text includes entries"
    fi
    if "$BIN" task list --root "$ROOT" --line a | grep -q 'T1'; then
        pass "task list text includes entries"
    else
        fail "task list text includes entries"
    fi
    rm -rf "$ROOT"
}

test_list_color() {
    if [ "$("$BIN" plan list --root "$PWD" | grep -c $'\033' || true)" = "0" ]; then
        pass "list output has no color when piped"
    else
        fail "list output has no color when piped"
    fi
    if command -v script >/dev/null 2>&1; then
        if script -qec "'$BIN' plan list --root '$PWD'" /dev/null 2>/dev/null | grep -q $'\033\[32m'; then
            pass "done is green on a tty"
        else
            fail "done is green on a tty"
        fi
        if NO_COLOR=1 script -qec "'$BIN' plan list --root '$PWD'" /dev/null 2>/dev/null | grep -q $'\033'; then
            fail "NO_COLOR disables color"
        else
            pass "NO_COLOR disables color"
        fi
    else
        pass "tty color checks skipped (script unavailable)"
        pass "NO_COLOR check skipped (script unavailable)"
    fi
}

test_color_config() {
    setup_project
    "$BIN" task add "t" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    jq '.color="always"' "$ROOT/.sele/config.json" > "$ROOT/.sele/config.json.tmp" &&
        mv "$ROOT/.sele/config.json.tmp" "$ROOT/.sele/config.json"
    if "$BIN" task list --root "$ROOT" --line a | grep -q $'\033\[36m'; then
        pass "color=always forces colors through a pipe"
    else
        fail "color=always forces colors through a pipe"
    fi
    jq '.color="never"' "$ROOT/.sele/config.json" > "$ROOT/.sele/config.json.tmp" &&
        mv "$ROOT/.sele/config.json.tmp" "$ROOT/.sele/config.json"
    if command -v script >/dev/null 2>&1; then
        if script -qec "'$BIN' task list --root '$ROOT' --line a" /dev/null 2>/dev/null | grep -q $'\033'; then
            fail "color=never disables colors on a tty"
        else
            pass "color=never disables colors on a tty"
        fi
    else
        pass "color=never check skipped (script unavailable)"
    fi
    rm -rf "$ROOT"
}

test_init() {
    local root
    root=$(mktemp -d)
    "$BIN" init --root "$root" >/dev/null
    if [ -d "$root/.sele/plans" ] && [ -d "$root/.sele/lines" ] &&
        [ -f "$root/.sele/log.jsonl" ]; then
        pass "init creates the .sele layout"
    else
        fail "init creates the .sele layout"
    fi
    rm -rf "$root"
}

test_version() {
    if "$BIN" --version | grep -q '0.21'; then
        pass "version reports 0.21"
    else
        fail "version reports 0.20.0"
    fi
}

test_plan_flow() {
    setup_project
    if "$BIN" plan show p --root "$ROOT" --output json | jq -e '.status == "active"' >/dev/null; then
        pass "plan new/submit/approve reaches active"
    else
        fail "plan new/submit/approve reaches active"
    fi
    rm -rf "$ROOT"

    local root
    root=$(mktemp -d)
    "$BIN" init --root "$root" >/dev/null
    "$BIN" plan new p --root "$root" --goal "g" --acceptance "a" --lines a >/dev/null
    if expect_code 2 "$BIN" plan approve p --root "$root" --by human:alice; then
        pass "approve rejects a draft plan"
    else
        fail "approve rejects a draft plan"
    fi
    rm -rf "$root"
}

test_line_requires_active_plan() {
    local root
    root=$(mktemp -d)
    "$BIN" init --root "$root" >/dev/null
    "$BIN" plan new p --root "$root" --goal "g" --acceptance "a" --lines a >/dev/null
    if expect_code 2 "$BIN" line new a --root "$root" --plan p --goal "ga" --acceptance "la" \
        --modules core; then
        pass "line new requires an active plan"
    else
        fail "line new requires an active plan"
    fi
    rm -rf "$root"
}

test_task_step_flow() {
    setup_project
    "$BIN" task add "login" --root "$ROOT" --line a --acceptance "login works" >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "schema" --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "api" --root "$ROOT" --line a --requires-review >/dev/null
    "$BIN" step start T1 S1 --root "$ROOT" --line a >/dev/null
    "$BIN" step done T1 S1 --root "$ROOT" --line a >/dev/null
    "$BIN" step start T1 S2 --root "$ROOT" --line a >/dev/null
    "$BIN" step done T1 S2 --root "$ROOT" --line a >/dev/null
    "$BIN" step approve T1 S2 --root "$ROOT" --line a >/dev/null
    "$BIN" task done T1 --root "$ROOT" --line a >/dev/null
    if "$BIN" task list --root "$ROOT" --line a --output json |
        jq -e '.items[0].status == "pending_review"' >/dev/null; then
        pass "task done goes to pending_review when acceptance is unverified"
    else
        fail "task done goes to pending_review when acceptance is unverified"
    fi
    "$BIN" task verify T1 A1 --root "$ROOT" --line a --by human:alice >/dev/null
    "$BIN" task done T1 --root "$ROOT" --line a >/dev/null
    if "$BIN" task list --root "$ROOT" --line a --output json |
        jq -e '.items[0].status == "done"' >/dev/null; then
        pass "task done after human verification"
    else
        fail "task done after human verification"
    fi
    rm -rf "$ROOT"
}

test_i1_auto_pause() {
    setup_project
    "$BIN" task add "one" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "two" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" task start T2 --root "$ROOT" --line a >/dev/null
    if "$BIN" task list --root "$ROOT" --line a --output json |
        jq -e '[.items[] | select(.id == "T1")][0].status == "paused"' >/dev/null; then
        pass "starting another task auto-pauses the current one (I1)"
    else
        fail "starting another task auto-pauses the current one (I1)"
    fi
    rm -rf "$ROOT"
}

test_i2_single_step() {
    setup_project
    "$BIN" task add "one" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "one" --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "two" --root "$ROOT" --line a >/dev/null
    "$BIN" step start T1 S1 --root "$ROOT" --line a >/dev/null
    if expect_code 2 "$BIN" step start T1 S2 --root "$ROOT" --line a; then
        pass "a second in-progress step is rejected (I2)"
    else
        fail "a second in-progress step is rejected (I2)"
    fi
    rm -rf "$ROOT"
}

test_task_done_requires_steps() {
    setup_project
    "$BIN" task add "one" --root "$ROOT" --line a >/dev/null
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" step add T1 "one" --root "$ROOT" --line a >/dev/null
    if expect_code 2 "$BIN" task done T1 --root "$ROOT" --line a; then
        pass "task done requires every step done/skipped"
    else
        fail "task done requires every step done/skipped"
    fi
    rm -rf "$ROOT"
}

test_next() {
    setup_project
    "$BIN" task add "one" --root "$ROOT" --line a >/dev/null
    "$BIN" task add "two" --root "$ROOT" --line a --depends T1 >/dev/null
    if "$BIN" next --root "$ROOT" --output json |
        jq -e '[.items[] | select(.id == "T2")] | length == 0' >/dev/null; then
        pass "next hides tasks with unmet dependencies"
    else
        fail "next hides tasks with unmet dependencies"
    fi
    "$BIN" task start T1 --root "$ROOT" --line a >/dev/null
    "$BIN" task done T1 --root "$ROOT" --line a >/dev/null
    if "$BIN" next --root "$ROOT" --output json |
        jq -e '[.items[] | select(.id == "T2")] | length == 1' >/dev/null; then
        pass "next shows a task once dependencies are done"
    else
        fail "next shows a task once dependencies are done"
    fi
    rm -rf "$ROOT"
}

test_check() {
    setup_project
    if "$BIN" check --root "$ROOT" --output json | jq -e '.ok == true' >/dev/null; then
        pass "check passes on a healthy project"
    else
        fail "check passes on a healthy project"
    fi
    rm "$ROOT/.sele/lines/p.a.json"
    if expect_code 3 "$BIN" check --root "$ROOT"; then
        pass "check fails when a line referenced by the plan is missing"
    else
        fail "check fails when a line referenced by the plan is missing"
    fi
    rm -rf "$ROOT"
}

test_log_and_tree() {
    setup_project
    "$BIN" task add "one" --root "$ROOT" --line a >/dev/null
    if "$BIN" log --root "$ROOT" --output json | jq -e '.items | length > 0' >/dev/null; then
        pass "log appends events"
    else
        fail "log appends events"
    fi
    if "$BIN" tree --root "$ROOT" --output json | jq -e '.plans[0].lines[0].tasks[0].id == "T1"' >/dev/null; then
        pass "tree renders plan/line/task"
    else
        fail "tree renders plan/line/task"
    fi
    rm -rf "$ROOT"
}

test_init
test_version
test_plan_flow
test_line_requires_active_plan
test_task_step_flow
test_i1_auto_pause
test_i2_single_step
test_task_done_requires_steps
test_next
test_check
test_log_and_tree
test_git_commit
test_git_drift_d2
test_plan_archive_restore
test_cache_index
test_log_compact
test_decision_replay
test_task_insert_move
test_cross_line_dep
test_pause_snapshot
test_skipped_count
test_step_lock
test_step_update_delete_move
test_full_ref
test_next_i1
test_p1_commands
test_depends_uid
test_check_extended
test_query_options
test_config
test_git_helpers
test_agent_prompt
test_list_text
test_list_color
test_color_config

echo
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
