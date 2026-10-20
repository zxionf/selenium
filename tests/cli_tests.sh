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
    sleep 1
    "$DESIGN_BIN" --root "$proj" close "$id" --dry-run --output json >/dev/null
    local mtime3=$(stat -c %Y "$proj/.design/changes/$id/design.md")
    local mtime4=$(stat -c %Y "$proj/.agent/work/$id/tasks.md")
    [[ $mtime1 -eq $mtime3 ]]
    [[ $mtime2 -eq $mtime4 ]]
    rm -rf "$proj"
}

# Test 10: close without dry-run
test_close_no_dry_run() {
    local proj=$(setup_project)
    local id=$("$DESIGN_BIN" --root "$proj" new --slug demo-change --output json | jq -r '.id')
    local out=$("$DESIGN_BIN" --root "$proj" close "$id" --output json 2>&1 || true)
    echo "$out" | jq -e '.error' >/dev/null
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
run_test "close without dry-run" test_close_no_dry_run

echo "=== All tests passed! ==="