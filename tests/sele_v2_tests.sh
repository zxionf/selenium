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

test_git_commit_checkpoint() {
    setup_git_project
    echo "b" > "$ROOT/b.txt"
    if "$BIN" git commit --root "$ROOT" --task T1 --step S1 --message "feat: add b" \
        --files b.txt --output json | jq -e '.checkpoint | startswith("ck-")' >/dev/null; then
        pass "git commit creates a checkpoint"
    else
        fail "git commit creates a checkpoint"
    fi
    if ls "$ROOT/.sele/checkpoints"/*/*.json >/dev/null 2>&1; then
        pass "checkpoint file is written"
    else
        fail "checkpoint file is written"
    fi
    "$BIN" status --root "$ROOT" >/dev/null
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

test_init() {
    local root
    root=$(mktemp -d)
    "$BIN" init --root "$root" >/dev/null
    if [ -d "$root/.sele/plans" ] && [ -d "$root/.sele/lines" ] && [ -d "$root/.sele/tasks" ] &&
        [ -d "$root/.sele/checkpoints" ] && [ -f "$root/.sele/log.jsonl" ]; then
        pass "init creates the .sele layout"
    else
        fail "init creates the .sele layout"
    fi
    rm -rf "$root"
}

test_version() {
    if "$BIN" --version | grep -q '0.20.0'; then
        pass "version reports 0.20.0"
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
test_git_commit_checkpoint
test_git_drift_d2

echo
echo "Passed: $PASS  Failed: $FAIL"
[ "$FAIL" -eq 0 ]
