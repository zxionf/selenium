#!/usr/bin/env bash
set -euo pipefail

DESIGN_BIN="$1"
if [[ ! -x "$DESIGN_BIN" ]]; then
    echo "Usage: $0 <path-to-design-binary>"
    exit 1
fi

# Test helper
run_test() {
    local name="$1"
    shift
    echo "=== Test: $name ==="
    "$@"
    echo "PASS: $name"
    echo
}

# Create temp project root
setup_project() {
    local tmpdir=$(mktemp -d)
    mkdir -p "$tmpdir/.design/changes"
    mkdir -p "$tmpdir/.agent/work"
    echo "# Design Index" > "$tmpdir/.design/INDEX.md"
    echo "# Spec" > "$tmpdir/.design/SPEC.md"
    echo "# Agent Index" > "$tmpdir/.agent/INDEX.md"
    echo "# Contract" > "$tmpdir/.agent/CONTRACT.md"
    echo "# Agent State

Active Change: 无" > "$tmpdir/.agent/STATE.md"
    echo "$tmpdir"
}

# Test 1: new success
test_new_success() {
    local proj=$(setup_project)
    local out=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json)
    echo "$out" | jq -e '.id | test("^[0-9]{8}-demo-change$")' >/dev/null
    local id=$(echo "$out" | jq -r '.id')
    [[ -d "$proj/.design/changes/$id" ]]
    [[ -d "$proj/.agent/work/$id" ]]
    [[ -f "$proj/.design/changes/$id/design.md" ]]
    [[ -f "$proj/.agent/work/$id/tasks.md" ]]
    grep -q "Active Change: $id" "$proj/.agent/STATE.md"
    rm -rf "$proj"
}

# Test 2: new duplicate ID
test_new_duplicate() {
    local proj=$(setup_project)
    "$DESIGN_BIN" --root "$proj" new --slug demo-change --output json >/dev/null
    local out=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json 2>&1 || true)
    echo "$out" | jq -e '.error' >/dev/null
    rm -rf "$proj"
}

# Test 3: new invalid slug
test_new_invalid_slug() {
    local proj=$(setup_project)
    local out=$("$DESIGN_BIN" --root "$proj" new --slug "Invalid_Slug" --output json 2>&1 || true)
    echo "$out" | jq -e '.error' >/dev/null
    rm -rf "$proj"
}

# Test 4: append-decision success
test_append_decision_success() {
    local proj=$(setup_project)
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    "$DESIGN_BIN" --root "$proj" append-decision \
        --context "Test context" \
        --decision "Test decision" \
        --consequences "Test consequences" \
        --change-id "$id" \
        --output json >/dev/null
    "$DESIGN_BIN" --root "$proj" append-decision \
        --context "Test context 2" \
        --decision "Test decision 2" \
        --consequences "Test consequences 2" \
        --output json >/dev/null
    local count=$(grep -c "^## \[" "$proj/.design/DECISIONS.log")
    [[ $count -eq 2 ]]
    rm -rf "$proj"
}

# Test 5: append-decision missing params
test_append_decision_missing() {
    local proj=$(setup_project)
    local out=$("$DESIGN_BIN" --root "$proj" append-decision --context "c" --decision "d" --output json 2>&1 || true)
    echo "$out" | jq -e '.error' >/dev/null
    rm -rf "$proj"
}

# Test 6: status JSON output
test_status_json() {
    local proj=$(setup_project)
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    # Add a task as done
    sed -i 's/- \[ \] 步骤 1/- [x] 步骤 1/' "$proj/.agent/work/$id/tasks.md"
    local out=$("$DESIGN_BIN" --root "$proj" status --output json)
    echo "$out" | jq -e --arg id "$id" '.active_id == $id' >/dev/null
    echo "$out" | jq -e '.tasks.total == 2' >/dev/null
    echo "$out" | jq -e '.tasks.done == 1' >/dev/null
    rm -rf "$proj"
}

# Test 7: lint ID inconsistency
test_lint_id_inconsistency() {
    local proj=$(setup_project)
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    # Remove work dir to create inconsistency
    rm -rf "$proj/.agent/work/$id"
    local out=$("$DESIGN_BIN" --root "$proj" lint --output json 2>&1 || true)
    echo "$out" | jq -e '.passed == false' >/dev/null
    echo "$out" | jq -e '.checks[] | select(.name=="id_consistency") | .passed == false' >/dev/null
    rm -rf "$proj"
}

# Test 8: lint SPEC.md process words
test_lint_spec_words() {
    local proj=$(setup_project)
    echo "TODO: fix this" >> "$proj/.design/SPEC.md"
    local out=$("$DESIGN_BIN" --root "$proj" lint --output json 2>&1 || true)
    echo "$out" | jq -e '.passed == false' >/dev/null
    echo "$out" | jq -e '.checks[] | select(.name=="spec_process_words") | .passed == false' >/dev/null
    rm -rf "$proj"
}

# Test 9: close dry-run
test_close_dry_run() {
    local proj=$(setup_project)
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    local out=$("$DESIGN_BIN" --root "$proj" close "$id" --dry-run --output json)
    echo "$out" | jq -e '.dry_run == true' >/dev/null
    echo "$out" | jq -e --arg id "$id" '.id == $id' >/dev/null
    # Verify no files modified
    local mtime1=$(stat -c %Y "$proj/.design/changes/$id/design.md")
    local mtime2=$(stat -c %Y "$proj/.agent/work/$id/tasks.md")
    local mtime3=$(stat -c %Y "$proj/.design/SPEC.md")
    local mtime4=$(stat -c %Y "$proj/.agent/STATE.md")
    sleep 1
    "$DESIGN_BIN" --root "$proj" close "$id" --dry-run --output json >/dev/null
    [[ $(stat -c %Y "$proj/.design/changes/$id/design.md") -eq $mtime1 ]]
    [[ $(stat -c %Y "$proj/.agent/work/$id/tasks.md") -eq $mtime2 ]]
    [[ $(stat -c %Y "$proj/.design/SPEC.md") -eq $mtime3 ]]
    [[ $(stat -c %Y "$proj/.agent/STATE.md") -eq $mtime4 ]]
    rm -rf "$proj"
}

# Test 10: close folds design.md into SPEC.md and resets STATE.md
test_close_folds_design() {
    local proj=$(setup_project)
    printf '\n## 目标\n\n旧的目标。\n' >> "$proj/.design/SPEC.md"
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    cat > "$proj/.design/changes/$id/design.md" <<EOF
# $id

## Context

需要一个新的状态视图。

## 影响面

- 第一层
  - 第二层

## 目标

让 status 默认有输出。
EOF

    local out=$("$DESIGN_BIN" --root "$proj" close "$id" --output json)
    echo "$out" | jq -e '.dry_run == false' >/dev/null
    echo "$out" | jq -e '.state_updated == true' >/dev/null
    echo "$out" | jq -e --arg id "$id" '.id == $id' >/dev/null

    grep -q '让 status 默认有输出' "$proj/.design/SPEC.md"
    ! grep -q '旧的目标' "$proj/.design/SPEC.md"
    grep -q '需要一个新的状态视图' "$proj/.design/SPEC.md"
    grep -q '^  - 第二层$' "$proj/.design/SPEC.md"

    grep -q 'Active Change: 无' "$proj/.agent/STATE.md"
    [[ -f "$proj/.design/changes/$id/design.md" ]]
    [[ -f "$proj/.agent/work/$id/tasks.md" ]]
    rm -rf "$proj"
}

# Test 13: dry-run must not invent a mapping for unmatched sections
test_close_dry_run_no_invented_mapping() {
    local proj=$(setup_project)
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    local out=$("$DESIGN_BIN" --root "$proj" close "$id" --dry-run --output json)
    echo "$out" | jq -e '.suggested_mapping | length > 0' >/dev/null
    echo "$out" | jq -e '[.suggested_mapping[]] | all(. == "")' >/dev/null
    rm -rf "$proj"
}

# Test 14: closing a non-active change leaves the active pointer alone
test_close_foreign_pointer() {
    local proj=$(setup_project)
    local first=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    local second=$("$DESIGN_BIN" --root "$proj" new --slug other-change --output json | jq -r '.id')
    local out=$("$DESIGN_BIN" --root "$proj" close "$first" --output json)
    echo "$out" | jq -e '.state_updated == false' >/dev/null
    grep -q "$second" "$proj/.agent/STATE.md"
    rm -rf "$proj"
}

# Test 11: status text output (default format must not be silent)
test_status_text() {
    local proj=$(setup_project)
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    local out=$("$DESIGN_BIN" --root "$proj" status)
    [[ -n "$out" ]]
    echo "$out" | grep -q "Active change: $id"
    echo "$out" | grep -q "Tasks: 0/2 done"
    echo "$out" | grep -q "Consistent: yes"
    rm -rf "$proj"
}

# Test 12: every command produces text output in default format
test_text_for_all_commands() {
    local fresh=$(mktemp -d)
    rmdir "$fresh"
    local init_out=$("$DESIGN_BIN" init "$fresh")
    echo "$init_out" | grep -q '^Initialized '
    rm -rf "$fresh"

    local proj=$(setup_project)
    local new_out=$("$DESIGN_BIN" --root "$proj" new --slug demo-change)
    echo "$new_out" | grep -q '^Created '
    local id=$(echo "$new_out" | head -n 1 | awk '{print $2}')

    local out=$("$DESIGN_BIN" --root "$proj" append-decision --context c --decision d --consequences e)
    echo "$out" | grep -q '^Appended decision #1'

    out=$("$DESIGN_BIN" --root "$proj" lint)
    echo "$out" | grep -q '^Lint passed'

    out=$("$DESIGN_BIN" --root "$proj" close "$id" --dry-run)
    echo "$out" | grep -q "^Close preview for $id"

    rm -rf "$proj"
}

# Run all tests
run_test "new success" test_new_success
run_test "new duplicate ID" test_new_duplicate
run_test "new invalid slug" test_new_invalid_slug
run_test "append-decision success" test_append_decision_success
run_test "append-decision missing params" test_append_decision_missing
run_test "status JSON output" test_status_json
run_test "lint ID inconsistency" test_lint_id_inconsistency
run_test "lint SPEC.md process words" test_lint_spec_words
run_test "close dry-run" test_close_dry_run
run_test "close without dry-run" test_close_folds_design
run_test "status text output" test_status_text
run_test "text output for all commands" test_text_for_all_commands
run_test "dry-run does not invent mapping" test_close_dry_run_no_invented_mapping
run_test "close leaves foreign active pointer" test_close_foreign_pointer

echo "=== All tests passed! ==="