#!/usr/bin/env bash
set -euo pipefail

BIN="$1"
if [[ ! -x "$BIN" ]]; then
    echo "Usage: $0 <path-to-sele-binary>"
    exit 1
fi

run_test() {
    local name="$1"
    shift
    echo "=== Test: $name ==="
    "$@"
    echo "PASS: $name"
    echo
}

# Create a git-backed project with .sele/ initialized.
setup_next() {
    local proj
    proj=$(mktemp -d)
    git -C "$proj" init -q -b main
    git -C "$proj" config user.email test@example.com
    git -C "$proj" config user.name tester
    echo seed > "$proj/seed.txt"
    git -C "$proj" add -A
    git -C "$proj" commit -qm init
    "$BIN" --root "$proj" init >/dev/null
    echo "$proj"
}

new_line() {
    local proj="$1"
    "$BIN" --root "$proj" line new demo --goal "demo goal" \
        --acceptance "first::ctx" --modules auth --output json
}

line_id() {
    local proj="$1"
    "$BIN" --root "$proj" line list --output json | jq -r '.items[0].id'
}

expect_code() {
    local expected="$1"
    shift
    set +e
    "$@" >/dev/null 2>&1
    local code=$?
    set -e
    if [[ $code -ne $expected ]]; then
        echo "expected exit $expected, got $code: $*" >&2
        exit 1
    fi
}

rebase_active() {
    local proj="$1" path
    for name in rebase-merge rebase-apply; do
        path=$(git -C "$proj" rev-parse --git-path "$name")
        if [[ "$path" = /* ]]; then
            [[ -e "$path" ]] && return 0
        else
            [[ -e "$proj/$path" ]] && return 0
        fi
    done
    return 1
}

# Draft line with one task, opened and running.
setup_running_line() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" task start T1 >/dev/null
    echo "$proj"
}

# Drive a running line to done and print its id.
finish_line() {
    local proj="$1"
    local id
    id=$(line_id "$proj")
    echo change >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T1 >/dev/null
    "$BIN" --root "$proj" task done T1 >/dev/null
    "$BIN" --root "$proj" acceptance pass A1 --evidence "ok" >/dev/null
    "$BIN" --root "$proj" line done >/dev/null
    echo "$id"
}

test_init_creates_sele() {
    local proj
    proj=$(mktemp -d)
    "$BIN" --root "$proj" init >/dev/null
    [[ -f "$proj/.sele/config.json" ]]
    [[ -f "$proj/.sele/log.jsonl" ]]
    [[ -f "$proj/.sele/.gitignore" ]]
    [[ -f "$proj/.sele/.gitattributes" ]]
    [[ -d "$proj/.sele/lines" ]]
    [[ -d "$proj/.sele/runtime/sessions" ]]
    expect_code 2 "$BIN" --root "$proj" init
    rm -rf "$proj"
}

test_init_legacy() {
    local proj
    proj=$(mktemp -d)
    "$BIN" --root "$proj" init --legacy >/dev/null
    [[ -f "$proj/.design/SPEC.md" ]]
    [[ -f "$proj/.agent/STATE.md" ]]
    [[ ! -d "$proj/.sele" ]]
    rm -rf "$proj"
}

test_line_new() {
    local proj out id
    proj=$(setup_next)
    out=$(new_line "$proj")
    echo "$out" | jq -e '.line | test("^[0-9]{8}-demo$")' >/dev/null
    echo "$out" | jq -e '.status == "draft"' >/dev/null
    id=$(echo "$out" | jq -r '.line')
    [[ -f "$proj/.sele/lines/$id/line.json" ]]
    jq -e --arg id "$id" '.line == $id' "$proj/.sele/runtime/sessions/default.json" >/dev/null
    jq -e '.acceptance[0].id == "A1"' "$proj/.sele/lines/$id/line.json" >/dev/null
    rm -rf "$proj"
}

test_line_duplicate() {
    local proj
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    expect_code 2 "$BIN" --root "$proj" line new demo --goal "g" \
        --acceptance "d::c" --modules auth
    rm -rf "$proj"
}

test_line_invalid_slug() {
    local proj
    proj=$(setup_next)
    expect_code 1 "$BIN" --root "$proj" line new Bad_Slug --goal "g" \
        --acceptance "d::c" --modules auth
    expect_code 1 "$BIN" --root "$proj" line new ok --goal "g" \
        --acceptance "no-separator" --modules auth
    rm -rf "$proj"
}

test_open_requires_task() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    expect_code 2 "$BIN" --root "$proj" line open "$id"
    rm -rf "$proj"
}

test_task_chain() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" task add "second" --modules auth >/dev/null
    jq -e '.depends == []' "$proj/.sele/lines/$id/tasks/T1.json" >/dev/null
    jq -e '.depends == ["T1"]' "$proj/.sele/lines/$id/tasks/T2.json" >/dev/null
    expect_code 1 "$BIN" --root "$proj" task add "third" --modules auth --depends T1
    rm -rf "$proj"
}

test_open_success() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    jq -e '.status == "active"' "$proj/.sele/lines/$id/line.json" >/dev/null
    jq -e '.frozen.acceptance_sha256 | length == 64' "$proj/.sele/lines/$id/line.json" >/dev/null
    git -C "$proj" rev-parse --verify "refs/heads/sele/$id" >/dev/null
    rm -rf "$proj"
}

test_task_start_idempotent() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" task start T1 >/dev/null
    "$BIN" --root "$proj" task start T1 --output json | jq -e '.changed == false' >/dev/null
    rm -rf "$proj"
}

test_single_running() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" task add "second" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" task start T1 >/dev/null
    expect_code 2 "$BIN" --root "$proj" task start T2
    rm -rf "$proj"
}

test_done_requires_checkpoint() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" task start T1 >/dev/null
    expect_code 2 "$BIN" --root "$proj" task done T1
    rm -rf "$proj"
}

test_checkpoint_and_done() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" task start T1 >/dev/null
    echo change >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T1 --msg "wip" >/dev/null
    "$BIN" --root "$proj" task done T1 >/dev/null
    jq -e '.tasks.T1.status == "done"' "$proj/.sele/runtime/state/$id.json" >/dev/null
    jq -e '.tasks.T1.checkpoint | length > 0' "$proj/.sele/runtime/state/$id.json" >/dev/null
    grep -q '"op": "task.done"\|"op":"task.done"' "$proj/.sele/log.jsonl"
    rm -rf "$proj"
}

test_line_done_requires_acceptance() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" task start T1 >/dev/null
    echo change >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T1 >/dev/null
    "$BIN" --root "$proj" task done T1 >/dev/null
    expect_code 2 "$BIN" --root "$proj" line done
    "$BIN" --root "$proj" acceptance pass A1 --evidence "ctx ok" >/dev/null
    "$BIN" --root "$proj" line done >/dev/null
    jq -e '.status == "done"' "$proj/.sele/lines/$id/line.json" >/dev/null
    rm -rf "$proj"
}

test_reopen_allows_new_task() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" task start T1 >/dev/null
    echo change >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T1 >/dev/null
    "$BIN" --root "$proj" task done T1 >/dev/null
    "$BIN" --root "$proj" acceptance pass A1 --evidence "ok" >/dev/null
    "$BIN" --root "$proj" line done >/dev/null
    "$BIN" --root "$proj" task add "late" --modules auth >/dev/null 2>&1 || true
    "$BIN" --root "$proj" line reopen "$id" --reason "regression" >/dev/null
    "$BIN" --root "$proj" task add "late" --modules auth >/dev/null
    jq -e '.depends == ["T1"]' "$proj/.sele/lines/$id/tasks/T2.json" >/dev/null
    rm -rf "$proj"
}

test_amend_refreezes() {
    local proj id before after
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    before=$(jq -r '.frozen.acceptance_sha256' "$proj/.sele/lines/$id/line.json")
    "$BIN" --root "$proj" line amend "$id" --reason "scope" \
        --acceptance "second::ctx2" >/dev/null
    after=$(jq -r '.frozen.acceptance_sha256' "$proj/.sele/lines/$id/line.json")
    [[ "$before" != "$after" ]]
    "$BIN" --root "$proj" line check "$id" >/dev/null
    rm -rf "$proj"
}

test_line_check_passes() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$id" >/dev/null
    "$BIN" --root "$proj" line check "$id" >/dev/null
    rm -rf "$proj"
}

test_line_check_fails_offline_chain() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    # Forge a broken chain: T1 depends on T2 while T2 depends on T1 would loop;
    # instead make the root depend on a later task.
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    local file="$proj/.sele/lines/$id/tasks/T1.json"
    jq '.depends = ["T9"]' "$file" > "$file.tmp" && mv "$file.tmp" "$file"
    expect_code 3 "$BIN" --root "$proj" line check "$id"
    rm -rf "$proj"
}

test_fsck_and_corrupt_log() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" fsck >/dev/null
    echo '{not json' >> "$proj/.sele/log.jsonl"
    expect_code 3 "$BIN" --root "$proj" fsck
    local json_file
    json_file=$(mktemp)
    "$BIN" --root "$proj" fsck --output json > "$json_file" || true
    jq -e '.passed == false' "$json_file" >/dev/null
    rm -f "$json_file"
    rm -rf "$proj"
}

test_mode_dispatch() {
    local proj legacy
    proj=$(setup_next)
    local id
    id=$(line_id "$proj")
    expect_code 4 "$BIN" --root "$proj" close "$id"
    expect_code 4 "$BIN" --root "$proj" lint
    expect_code 4 "$BIN" --root "$proj" status

    legacy=$(mktemp -d)
    mkdir -p "$legacy/.design/changes" "$legacy/.agent/work"
    printf '# Spec\n' > "$legacy/.design/SPEC.md"
    printf '# State\n\nActive Change: 无\n' > "$legacy/.agent/STATE.md"
    expect_code 4 "$BIN" --root "$legacy" line list
    expect_code 4 "$BIN" --root "$legacy" task list
    rm -rf "$proj" "$legacy"
}

test_text_outputs() {
    local proj id out
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null

    out=$("$BIN" --root "$proj" line list)
    [[ -n "$out" ]]
    out=$("$BIN" --root "$proj" line show "$id")
    [[ -n "$out" ]]
    out=$("$BIN" --root "$proj" task list)
    [[ -n "$out" ]]
    out=$("$BIN" --root "$proj" overview)
    [[ -n "$out" ]]
    out=$("$BIN" --root "$proj" log)
    [[ -n "$out" ]]

    "$BIN" --root "$proj" line list --output json | jq -e '.items | length == 1' >/dev/null
    "$BIN" --root "$proj" line show "$id" --output json | jq -e '.tasks | length == 1' >/dev/null
    "$BIN" --root "$proj" task list --output json | jq -e '.items[0].id == "T1"' >/dev/null
    "$BIN" --root "$proj" overview --output json | jq -e '.current == $id' --arg id "$id" >/dev/null
    rm -rf "$proj"
}

test_session_isolation() {
    local proj a b
    proj=$(setup_next)
    a=$(new_line "$proj" | jq -r '.line')
    b=$("$BIN" --root "$proj" line new other --goal "other" --acceptance "d::c" \
        --modules auth --output json | jq -r '.line')
    jq -e --arg id "$b" '.line == $id' "$proj/.sele/runtime/sessions/default.json" >/dev/null
    "$BIN" --root "$proj" --session s2 line switch "$a" >/dev/null
    jq -e --arg id "$a" '.line == $id' "$proj/.sele/runtime/sessions/s2.json" >/dev/null
    jq -e --arg id "$b" '.line == $id' "$proj/.sele/runtime/sessions/default.json" >/dev/null
    rm -rf "$proj"
}

test_task_fail_retry() {
    local proj id
    proj=$(setup_running_line)
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task fail T1 --reason "tests broke" >/dev/null
    jq -e '.tasks.T1.status == "failed" and .tasks.T1.reason == "tests broke"' \
        "$proj/.sele/runtime/state/$id.json" >/dev/null
    "$BIN" --root "$proj" task retry T1 >/dev/null
    jq -e '.tasks.T1.status == "running" and .tasks.T1.reason == null' \
        "$proj/.sele/runtime/state/$id.json" >/dev/null
    rm -rf "$proj"
}

test_task_block_unblock() {
    local proj id
    proj=$(setup_running_line)
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task block T1 --reason "needs review" >/dev/null
    jq -e '.tasks.T1.status == "blocked"' "$proj/.sele/runtime/state/$id.json" >/dev/null
    jq -e '.status == "active"' "$proj/.sele/lines/$id/line.json" >/dev/null
    "$BIN" --root "$proj" task unblock T1 >/dev/null
    jq -e '.tasks.T1.status == "running"' "$proj/.sele/runtime/state/$id.json" >/dev/null
    rm -rf "$proj"
}

test_invalid_task_transitions() {
    local proj id pending
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    expect_code 2 "$BIN" --root "$proj" task start T1
    expect_code 2 "$BIN" --root "$proj" task fail T1 --reason "late"
    rm -rf "$proj"

    pending=$(setup_next)
    new_line "$pending" >/dev/null
    "$BIN" --root "$pending" task add "first" --modules auth >/dev/null
    "$BIN" --root "$pending" line open "$(line_id "$pending")" >/dev/null
    expect_code 2 "$BIN" --root "$pending" task done T1
    rm -rf "$pending"
}

test_line_block_unblock() {
    local proj id
    proj=$(setup_running_line)
    id=$(line_id "$proj")
    "$BIN" --root "$proj" line block "$id" --reason "waiting for review" >/dev/null
    jq -e '.status == "blocked" and .blocked.reason == "waiting for review"' \
        "$proj/.sele/lines/$id/line.json" >/dev/null
    expect_code 2 "$BIN" --root "$proj" task start T1
    "$BIN" --root "$proj" line unblock "$id" >/dev/null
    jq -e '.status == "active" and .blocked == null' "$proj/.sele/lines/$id/line.json" >/dev/null
    rm -rf "$proj"
}

test_line_abandon() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" line abandon "$id" --reason "superseded" >/dev/null
    jq -e '.status == "abandoned"' "$proj/.sele/lines/$id/line.json" >/dev/null
    expect_code 2 "$BIN" --root "$proj" task add "late" --modules auth
    expect_code 2 "$BIN" --root "$proj" line open "$id"
    rm -rf "$proj"
}

test_open_rejects_dirty() {
    local proj id
    proj=$(setup_next)
    new_line "$proj" >/dev/null
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    echo dirty >> "$proj/seed.txt"
    expect_code 2 "$BIN" --root "$proj" line open "$id"
    rm -rf "$proj"
}

test_switch_rejects_dirty() {
    local proj a
    proj=$(setup_next)
    a=$(new_line "$proj" | jq -r '.line')
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$a" >/dev/null
    "$BIN" --root "$proj" line new other --goal "other" --acceptance "d::c" \
        --modules auth >/dev/null
    echo dirty >> "$proj/seed.txt"
    expect_code 2 "$BIN" --root "$proj" line switch "$a"
    rm -rf "$proj"
}

test_switch_rejects_done() {
    local proj id
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    expect_code 2 "$BIN" --root "$proj" line switch "$id"
    rm -rf "$proj"
}

test_check_strict_cross_line() {
    local proj a b
    proj=$(setup_next)
    a=$(new_line "$proj" | jq -r '.line')
    "$BIN" --root "$proj" task add "first" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$a" >/dev/null
    b=$("$BIN" --root "$proj" line new other --goal "other" --acceptance "d::c" \
        --modules auth --output json | jq -r '.line')
    "$BIN" --root "$proj" task add "second" --modules auth >/dev/null
    "$BIN" --root "$proj" line open "$b" >/dev/null
    expect_code 3 "$BIN" --root "$proj" line check "$a" --strict
    "$BIN" --root "$proj" line check "$a" >/dev/null
    rm -rf "$proj"
}

test_done_rejects_new_task() {
    local proj
    proj=$(setup_running_line)
    finish_line "$proj" >/dev/null
    expect_code 2 "$BIN" --root "$proj" task add "late" --modules auth
    rm -rf "$proj"
}

test_merge_success() {
    local proj id
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    "$BIN" --root "$proj" line merge "$id" --confirm >/dev/null
    jq -e '.status == "merged" and .merged.commit != null' \
        "$proj/.sele/lines/$id/line.json" >/dev/null
    [[ "$(git -C "$proj" branch --show-current)" == "main" ]]
    git -C "$proj" log --oneline --merges | grep -q "$id"
    grep -q '"op":"line.merge"' "$proj/.sele/log.jsonl"
    [[ -z "$(git -C "$proj" status --porcelain)" ]]
    rm -rf "$proj"
}

test_merge_conflict() {
    local proj id out code
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    git -C "$proj" switch main >/dev/null 2>&1
    echo main-side > "$proj/seed.txt"
    git -C "$proj" add -A
    git -C "$proj" commit -qm "main side"

    out=$(mktemp)
    set +e
    "$BIN" --root "$proj" line merge "$id" --confirm > "$out" 2>&1
    code=$?
    set -e
    [[ $code -eq 2 ]]
    grep -q "Merge conflict" "$out"
    rm -f "$out"

    jq -e '.status == "done" and .merged == null' "$proj/.sele/lines/$id/line.json" >/dev/null
    [[ "$(git -C "$proj" branch --show-current)" == "sele/$id" ]]
    [[ -z "$(git -C "$proj" status --porcelain)" ]]
    rm -rf "$proj"
}

test_merge_requires_done() {
    local proj id
    proj=$(setup_running_line)
    id=$(line_id "$proj")
    expect_code 2 "$BIN" --root "$proj" line merge "$id" --confirm
    rm -rf "$proj"
}

test_merge_requires_confirm() {
    local proj id
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    expect_code 1 "$BIN" --root "$proj" line merge "$id"
    jq -e '.status == "done"' "$proj/.sele/lines/$id/line.json" >/dev/null
    rm -rf "$proj"
}

test_merged_is_read_only() {
    local proj id
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    "$BIN" --root "$proj" line merge "$id" --confirm >/dev/null
    expect_code 2 "$BIN" --root "$proj" task add "late" --modules auth
    expect_code 2 "$BIN" --root "$proj" line switch "$id"
    expect_code 2 "$BIN" --root "$proj" line amend "$id" --reason "x" --goal "y"
    expect_code 2 "$BIN" --root "$proj" line done "$id"
    "$BIN" --root "$proj" line show "$id" | grep -q 'merged'
    rm -rf "$proj"
}

test_merge_delete_branch() {
    local proj id
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    "$BIN" --root "$proj" line merge "$id" --confirm --delete-branch >/dev/null
    jq -e '.status == "merged"' "$proj/.sele/lines/$id/line.json" >/dev/null
    set +e
    git -C "$proj" rev-parse --verify "refs/heads/sele/$id" >/dev/null 2>&1
    local code=$?
    set -e
    [[ $code -ne 0 ]]
    rm -rf "$proj"
}

test_line_revert() {
    local proj id
    proj=$(setup_running_line)
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "second" --modules auth >/dev/null
    echo one >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T1 >/dev/null
    "$BIN" --root "$proj" task done T1 >/dev/null
    "$BIN" --root "$proj" task start T2 >/dev/null
    echo two >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T2 >/dev/null
    "$BIN" --root "$proj" task done T2 >/dev/null
    "$BIN" --root "$proj" acceptance pass A1 --evidence ok >/dev/null
    "$BIN" --root "$proj" line done >/dev/null

    "$BIN" --root "$proj" line revert "$id" --to T1 >/dev/null
    jq -e '.status == "active"' "$proj/.sele/lines/$id/line.json" >/dev/null
    jq -e '.tasks.T1.status == "done"' "$proj/.sele/runtime/state/$id.json" >/dev/null
    jq -e '.tasks.T2.status == "pending" and .tasks.T2.checkpoint == null' \
        "$proj/.sele/runtime/state/$id.json" >/dev/null
    jq -e '.acceptance == {}' "$proj/.sele/runtime/state/$id.json" >/dev/null
    grep -q '^one$' "$proj/seed.txt"
    ! grep -q '^two$' "$proj/seed.txt"
    grep -q '"op":"line.revert"' "$proj/.sele/log.jsonl"

    expect_code 2 "$BIN" --root "$proj" line revert "$id" --to T2
    expect_code 2 "$BIN" --root "$proj" line revert "$id" --to T9
    rm -rf "$proj"
}

test_revert_rejects_merged() {
    local proj id
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    "$BIN" --root "$proj" line merge "$id" --confirm >/dev/null
    expect_code 2 "$BIN" --root "$proj" line revert "$id" --to T1
    rm -rf "$proj"
}

test_task_revert_middle() {
    local proj id
    proj=$(setup_running_line)
    id=$(line_id "$proj")
    "$BIN" --root "$proj" task add "second" --modules auth >/dev/null
    echo one >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T1 >/dev/null
    "$BIN" --root "$proj" task done T1 >/dev/null
    "$BIN" --root "$proj" task start T2 >/dev/null
    echo two >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T2 >/dev/null
    "$BIN" --root "$proj" task done T2 >/dev/null
    "$BIN" --root "$proj" acceptance pass A1 --evidence ok >/dev/null
    "$BIN" --root "$proj" line done >/dev/null

    "$BIN" --root "$proj" task revert T2 >/dev/null
    jq -e '.status == "active"' "$proj/.sele/lines/$id/line.json" >/dev/null
    jq -e '.tasks.T1.status == "done"' "$proj/.sele/runtime/state/$id.json" >/dev/null
    jq -e '.tasks.T2.status == "pending" and .tasks.T2.checkpoint == null' \
        "$proj/.sele/runtime/state/$id.json" >/dev/null
    jq -e '.acceptance == {}' "$proj/.sele/runtime/state/$id.json" >/dev/null
    grep -q '^one$' "$proj/seed.txt"
    ! grep -q '^two$' "$proj/seed.txt"
    grep -q '"op":"task.revert"' "$proj/.sele/log.jsonl"
    rm -rf "$proj"
}

test_task_revert_root() {
    local proj id
    proj=$(setup_running_line)
    id=$(line_id "$proj")
    echo one >> "$proj/seed.txt"
    "$BIN" --root "$proj" task checkpoint T1 >/dev/null
    "$BIN" --root "$proj" task done T1 >/dev/null
    "$BIN" --root "$proj" acceptance pass A1 --evidence ok >/dev/null
    "$BIN" --root "$proj" line done >/dev/null

    "$BIN" --root "$proj" task revert T1 >/dev/null
    jq -e '.status == "draft" and .frozen == null' "$proj/.sele/lines/$id/line.json" >/dev/null
    jq -e '.tasks.T1.status == "pending"' "$proj/.sele/runtime/state/$id.json" >/dev/null
    ! grep -q '^one$' "$proj/seed.txt"

    "$BIN" --root "$proj" line open "$id" >/dev/null
    jq -e '.status == "active"' "$proj/.sele/lines/$id/line.json" >/dev/null
    rm -rf "$proj"
}

test_task_revert_requires_checkpoint() {
    local proj
    proj=$(setup_running_line)
    "$BIN" --root "$proj" task add "second" --modules auth >/dev/null
    expect_code 2 "$BIN" --root "$proj" task revert T2
    rm -rf "$proj"
}

test_line_rebase_success() {
    local proj id main_head
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    git -C "$proj" switch main >/dev/null 2>&1
    echo extra > "$proj/extra.txt"
    git -C "$proj" add -A
    git -C "$proj" commit -qm "main extra"
    main_head=$(git -C "$proj" rev-parse --short HEAD)

    "$BIN" --root "$proj" line rebase "$id" >/dev/null
    git -C "$proj" merge-base --is-ancestor main "sele/$id"
    jq -e --arg c "$main_head" '.base_commit == $c' "$proj/.sele/lines/$id/line.json" >/dev/null
    [[ "$(git -C "$proj" branch --show-current)" == "sele/$id" ]]
    [[ -z "$(git -C "$proj" status --porcelain)" ]]
    grep -q '"op":"line.rebase"' "$proj/.sele/log.jsonl"
    rm -rf "$proj"
}

test_line_rebase_conflict_abort() {
    local proj id before
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    before=$(jq -r '.base_commit' "$proj/.sele/lines/$id/line.json")
    git -C "$proj" switch main >/dev/null 2>&1
    echo main-side >> "$proj/seed.txt"
    git -C "$proj" add -A
    git -C "$proj" commit -qm "main side"

    expect_code 2 "$BIN" --root "$proj" line rebase "$id"
    if ! rebase_active "$proj"; then echo "rebase not in progress" >&2; exit 1; fi

    "$BIN" --root "$proj" line rebase "$id" --abort >/dev/null
    if rebase_active "$proj"; then echo "rebase still in progress" >&2; exit 1; fi
    jq -e --arg c "$before" '.base_commit == $c' "$proj/.sele/lines/$id/line.json" >/dev/null
    [[ "$(git -C "$proj" branch --show-current)" == "sele/$id" ]]
    [[ -z "$(git -C "$proj" status --porcelain)" ]]
    rm -rf "$proj"
}

test_line_rebase_conflict_continue() {
    local proj id main_head
    proj=$(setup_running_line)
    id=$(finish_line "$proj")
    git -C "$proj" switch main >/dev/null 2>&1
    echo main-side >> "$proj/seed.txt"
    git -C "$proj" add -A
    git -C "$proj" commit -qm "main side"
    main_head=$(git -C "$proj" rev-parse --short HEAD)

    expect_code 2 "$BIN" --root "$proj" line rebase "$id"
    echo resolved > "$proj/seed.txt"
    git -C "$proj" add seed.txt
    "$BIN" --root "$proj" line rebase "$id" --continue >/dev/null

    if rebase_active "$proj"; then echo "rebase still in progress" >&2; exit 1; fi
    [[ -z "$(git -C "$proj" status --porcelain)" ]]
    jq -e --arg c "$main_head" '.base_commit == $c' "$proj/.sele/lines/$id/line.json" >/dev/null
    grep -q '"op":"line.rebase"' "$proj/.sele/log.jsonl"
    rm -rf "$proj"
}

run_test "init creates .sele" test_init_creates_sele
run_test "init --legacy creates .design/.agent" test_init_legacy
run_test "line new creates draft and session pointer" test_line_new
run_test "line new duplicate id" test_line_duplicate
run_test "line new invalid slug/acceptance" test_line_invalid_slug
run_test "line open requires a task" test_open_requires_task
run_test "task add builds an explicit chain" test_task_chain
run_test "line open freezes acceptance and creates branch" test_open_success
run_test "task start is idempotent" test_task_start_idempotent
run_test "only one running task per line" test_single_running
run_test "task done requires a checkpoint" test_done_requires_checkpoint
run_test "checkpoint then done records commit" test_checkpoint_and_done
run_test "line done requires acceptance" test_line_done_requires_acceptance
run_test "reopen allows a new task" test_reopen_allows_new_task
run_test "amend re-freezes acceptance" test_amend_refreezes
run_test "line check passes for a valid chain" test_line_check_passes
run_test "line check fails for a broken chain" test_line_check_fails_offline_chain
run_test "fsck detects a corrupt log" test_fsck_and_corrupt_log
run_test "legacy and next commands refuse each other" test_mode_dispatch
run_test "text and json outputs" test_text_outputs
run_test "sessions keep independent pointers" test_session_isolation
run_test "task fail then retry clears the reason" test_task_fail_retry
run_test "task block/unblock leaves the line active" test_task_block_unblock
run_test "invalid task transitions are rejected" test_invalid_task_transitions
run_test "line block/unblock gates task start" test_line_block_unblock
run_test "abandoned line rejects new tasks" test_line_abandon
run_test "line open rejects a dirty tree" test_open_rejects_dirty
run_test "line switch rejects a dirty tree" test_switch_rejects_dirty
run_test "line switch rejects a done line" test_switch_rejects_done
run_test "check --strict fails on cross-line modules" test_check_strict_cross_line
run_test "done line rejects new tasks" test_done_rejects_new_task
run_test "line merge succeeds and records the merge commit" test_merge_success
run_test "merge conflict aborts and keeps the line done" test_merge_conflict
run_test "merge requires a done line" test_merge_requires_done
run_test "merge requires --confirm" test_merge_requires_confirm
run_test "merged line is read-only" test_merged_is_read_only
run_test "merge --delete-branch removes the line branch" test_merge_delete_branch
run_test "line revert resets to a task checkpoint" test_line_revert
run_test "revert rejects merged lines" test_revert_rejects_merged
run_test "task revert undoes a middle task" test_task_revert_middle
run_test "task revert of the root task returns the line to draft" test_task_revert_root
run_test "task revert requires a predecessor checkpoint" test_task_revert_requires_checkpoint
run_test "line rebase moves the line onto main" test_line_rebase_success
run_test "rebase conflict can be aborted" test_line_rebase_conflict_abort
run_test "rebase conflict can be continued" test_line_rebase_conflict_continue

echo "=== All sele tests passed! ==="
