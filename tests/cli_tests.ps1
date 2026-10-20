#!/usr/bin/env pwsh

param(
    [Parameter(Mandatory=$true)]
    [string]$SeleBin
)

if (-not (Test-Path $SeleBin)) {
    Write-Error "Usage: $0 <path-to-sele-binary>"
    exit 1
}

function Run-Test {
    param(
        [string]$Name,
        [scriptblock]$Test
    )
    Write-Host "=== Test: $Name ===" -ForegroundColor Cyan
    try {
        & $Test
        Write-Host "PASS: $Name" -ForegroundColor Green
    } catch {
        Write-Host "FAIL: $Name - $_" -ForegroundColor Red
        exit 1
    }
    Write-Host ""
}

function Setup-Project {
    $tmpdir = [System.IO.Path]::GetTempFileName()
    Remove-Item $tmpdir
    New-Item -ItemType Directory -Path $tmpdir | Out-Null
    New-Item -ItemType Directory -Path "$tmpdir/.design/changes" | Out-Null
    New-Item -ItemType Directory -Path "$tmpdir/.agent/work" | Out-Null
    "@ Design Index" | Set-Content "$tmpdir/.design/INDEX.md"
    "@ Spec" | Set-Content "$tmpdir/.design/SPEC.md"
    "@ Agent Index" | Set-Content "$tmpdir/.agent/INDEX.md"
    "@ Contract" | Set-Content "$tmpdir/.agent/CONTRACT.md"
    "@ Agent State`n`nActive Change: 无" | Set-Content "$tmpdir/.agent/STATE.md"
    return $tmpdir
}

# Test 1: new success
Run-Test "new success" {
    $proj = Setup-Project
    $out = & $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json
    if ($out.id -notmatch '^\d{8}-demo-change$') { throw "ID mismatch: $($out.id)" }
    $id = $out.id
    if (-not (Test-Path "$proj/.design/changes/$id")) { throw "changes dir missing" }
    if (-not (Test-Path "$proj/.agent/work/$id")) { throw "work dir missing" }
    if (-not (Test-Path "$proj/.design/changes/$id/design.md")) { throw "design.md missing" }
    if (-not (Test-Path "$proj/.agent/work/$id/tasks.md")) { throw "tasks.md missing" }
    $state = Get-Content "$proj/.agent/STATE.md" -Raw
    if ($state -notmatch "Active Change: $id") { throw "STATE.md not updated" }
    Remove-Item -Recurse -Force $proj
}

# Test 2: new duplicate ID
Run-Test "new duplicate ID" {
    $proj = Setup-Project
    & $SeleBin --root $proj new --slug demo-change --output json | Out-Null
    $out = & $SeleBin --root $proj new --slug demo-change --output json 2>&1 | ConvertFrom-Json
    if (-not $out.error) { throw "Expected error for duplicate ID" }
    Remove-Item -Recurse -Force $proj
}

# Test 3: new invalid slug
Run-Test "new invalid slug" {
    $proj = Setup-Project
    $out = & $SeleBin --root $proj new --slug "Invalid_Slug" --output json 2>&1 | ConvertFrom-Json
    if (-not $out.error) { throw "Expected error for invalid slug" }
    if ($out.error -notmatch "lowercase") { throw "Error message should mention lowercase" }
    Remove-Item -Recurse -Force $proj
}

# Test 4: append-decision success
Run-Test "append-decision success" {
    $proj = Setup-Project
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    & $SeleBin --root $proj append-decision --context "Test context" --decision "Test decision" --consequences "Test consequences" --change-id $id --output json | Out-Null
    & $SeleBin --root $proj append-decision --context "Test context 2" --decision "Test decision 2" --consequences "Test consequences 2" --output json | Out-Null
    $count = (Get-Content "$proj/.design/DECISIONS.log" | Where-Object { $_ -match '^## \[' }).Count
    if ($count -ne 2) { throw "Expected 2 decisions, got $count" }
    Remove-Item -Recurse -Force $proj
}

# Test 5: append-decision missing params
Run-Test "append-decision missing params" {
    $proj = Setup-Project
    $out = & $SeleBin --root $proj append-decision --context "c" --decision "d" --output json 2>&1 | ConvertFrom-Json
    if (-not $out.error) { throw "Expected error for missing consequences" }
    Remove-Item -Recurse -Force $proj
}

# Test 6: status JSON output
Run-Test "status JSON output" {
    $proj = Setup-Project
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    # Mark first task as done
    $tasks = Get-Content "$proj/.agent/work/$id/tasks.md"
    $tasks = $tasks -replace '- \[ \] 步骤 1', '- [x] 步骤 1'
    $tasks | Set-Content "$proj/.agent/work/$id/tasks.md"
    $out = & $SeleBin --root $proj status --output json | ConvertFrom-Json
    if ($out.active_id -ne $id) { throw "Active ID mismatch" }
    if ($out.tasks.total -ne 2) { throw "Expected total=2, got $($out.tasks.total)" }
    if ($out.tasks.done -ne 1) { throw "Expected done=1, got $($out.tasks.done)" }
    Remove-Item -Recurse -Force $proj
}

# Test 7: lint ID inconsistency
Run-Test "lint ID inconsistency" {
    $proj = Setup-Project
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    Remove-Item -Recurse -Force "$proj/.agent/work/$id"
    $out = & $SeleBin --root $proj lint --output json 2>&1 | ConvertFrom-Json
    if ($out.passed) { throw "Expected lint to fail" }
    $check = $out.checks | Where-Object { $_.name -eq "id_consistency" }
    if ($check.passed) { throw "id_consistency should fail" }
    Remove-Item -Recurse -Force $proj
}

# Test 8: lint SPEC.md process words
Run-Test "lint SPEC.md process words" {
    $proj = Setup-Project
    "TODO: fix this" | Add-Content "$proj/.design/SPEC.md"
    $out = & $SeleBin --root $proj lint --output json 2>&1 | ConvertFrom-Json
    if ($out.passed) { throw "Expected lint to fail" }
    $check = $out.checks | Where-Object { $_.name -eq "spec_process_words" }
    if ($check.passed) { throw "spec_process_words should fail" }
    Remove-Item -Recurse -Force $proj
}

# Test 9: close dry-run
Run-Test "close dry-run" {
    $proj = Setup-Project
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    $out = & $SeleBin --root $proj close $id --dry-run --output json | ConvertFrom-Json
    if (-not $out.dry_run) { throw "dry_run should be true" }
    if ($out.id -ne $id) { throw "ID mismatch" }
    if (-not $out.plan) { throw "plan missing from dry-run JSON" }
    $actions = @($out.plan | ForEach-Object { $_.action })
    if ($actions -notcontains "skipped") { throw "template placeholder should be planned as skipped: $($actions -join ',')" }
    # Verify no files modified
    $mtime1 = (Get-Item "$proj/.design/changes/$id/design.md").LastWriteTime
    $mtime2 = (Get-Item "$proj/.agent/work/$id/tasks.md").LastWriteTime
    $mtime3 = (Get-Item "$proj/.design/SPEC.md").LastWriteTime
    $mtime4 = (Get-Item "$proj/.agent/STATE.md").LastWriteTime
    Start-Sleep -Seconds 1
    & $SeleBin --root $proj close $id --dry-run --output json | Out-Null
    if ((Get-Item "$proj/.design/changes/$id/design.md").LastWriteTime -ne $mtime1) { throw "design.md was modified" }
    if ((Get-Item "$proj/.agent/work/$id/tasks.md").LastWriteTime -ne $mtime2) { throw "tasks.md was modified" }
    if ((Get-Item "$proj/.design/SPEC.md").LastWriteTime -ne $mtime3) { throw "SPEC.md was modified" }
    if ((Get-Item "$proj/.agent/STATE.md").LastWriteTime -ne $mtime4) { throw "STATE.md was modified" }
    Remove-Item -Recurse -Force $proj
}

# Test 10: close folds design.md into SPEC.md and resets STATE.md
Run-Test "close folds design into SPEC" {
    $proj = Setup-Project
    Add-Content "$proj/.design/SPEC.md" "`n## 目标`n`n旧的目标。"
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    @"
# $id

## Context

需要一个新的状态视图。

## 影响面

- 第一层
  - 第二层

## 目标

让 status 默认有输出。
"@ | Set-Content "$proj/.design/changes/$id/design.md"

    $out = & $SeleBin --root $proj close $id --output json | ConvertFrom-Json
    if ($out.dry_run) { throw "dry_run should be false for a real close" }
    if (-not $out.state_updated) { throw "state_updated should be true" }
    if ($out.id -ne $id) { throw "ID mismatch" }

    $spec = Get-Content "$proj/.design/SPEC.md" -Raw
    if ($spec -notmatch '让 status 默认有输出') { throw "SPEC.md should contain the folded 目标 body" }
    if ($spec -match '旧的目标') { throw "folded section should replace the old body" }
    if ($spec -notmatch '需要一个新的状态视图') { throw "SPEC.md should gain the appended Context section" }
    if ($spec -notmatch '(?m)^  - 第二层$') { throw "nested list indentation must survive the fold" }

    $state = Get-Content "$proj/.agent/STATE.md" -Raw
    if ($state -notmatch 'Active Change: 无') { throw "STATE.md should be reset to 无" }
    if (-not (Test-Path "$proj/.design/changes/$id/design.md")) { throw "design.md should stay in place" }
    if (-not (Test-Path "$proj/.agent/work/$id/tasks.md")) { throw "tasks.md should stay in place" }

    # Re-closing an already folded change must not rewrite SPEC.md again
    $mtime = (Get-Item "$proj/.design/SPEC.md").LastWriteTime
    Start-Sleep -Seconds 1
    $again = & $SeleBin --root $proj close $id --output json | ConvertFrom-Json
    if ($again.spec_updated) { throw "re-closing an unchanged fold should report spec_updated=false" }
    if ((Get-Item "$proj/.design/SPEC.md").LastWriteTime -ne $mtime) { throw "SPEC.md was rewritten without changes" }
    Remove-Item -Recurse -Force $proj
}

# Test 16: duplicate section titles inside one design.md merge into one section
Run-Test "duplicate design titles merge" {
    $proj = Setup-Project
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    @"
# $id

## 新概念

甲。

## 新概念

乙。
"@ | Set-Content "$proj/.design/changes/$id/design.md"
    & $SeleBin --root $proj close $id --output json | Out-Null
    $spec = Get-Content "$proj/.design/SPEC.md" -Raw
    $count = ([regex]::Matches($spec, '(?m)^## 新概念')).Count
    if ($count -ne 1) { throw "expected one 新概念 section, got $count" }
    if ($spec -notmatch '乙。') { throw "the last body should win" }
    if ($spec -match '甲。') { throw "the earlier duplicate body should not survive" }
    Remove-Item -Recurse -Force $proj
}

# Test 17: an HTML comment followed by real text is content, not a placeholder
Run-Test "inline comment body is folded" {
    $proj = Setup-Project
    Add-Content "$proj/.design/SPEC.md" "`n## 目标`n`n旧目标。"
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    @"
# $id

## 目标

<!-- 说明 --> 真实内容。
"@ | Set-Content "$proj/.design/changes/$id/design.md"
    $out = & $SeleBin --root $proj close $id --output json | ConvertFrom-Json
    $action = ($out.folded | Where-Object { $_.section -eq "目标" }).action
    if ($action -ne "replaced") { throw "expected 目标 to be replaced, got '$action'" }
    $spec = Get-Content "$proj/.design/SPEC.md" -Raw
    if ($spec -notmatch '真实内容') { throw "text after an inline comment must reach SPEC.md" }
    if ($spec -match '旧目标') { throw "the old body should be replaced" }
    Remove-Item -Recurse -Force $proj
}

# Test 18: "## " inside a fenced code block is not a section boundary
Run-Test "fenced code survives the fold" {
    $proj = Setup-Project
    @'
## 输出

说明：

```text
## 内层标题
Active change: none
```

## 约束

旧约束。
'@ | Set-Content "$proj/.design/SPEC.md"
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    @"
# $id

## 约束

新约束。
"@ | Set-Content "$proj/.design/changes/$id/design.md"
    & $SeleBin --root $proj close $id --output json | Out-Null
    $spec = Get-Content "$proj/.design/SPEC.md" -Raw
    $fences = ([regex]::Matches($spec, '(?m)^```')).Count
    if ($fences -ne 2) { throw "the fenced block should keep both fence lines, got $fences" }
    if ($spec -notmatch '(?m)^## 内层标题\r?$') { throw "fenced content should stay verbatim" }
    if ($spec -notmatch '新约束') { throw "the 约束 section should be folded" }
    if ($spec -match '旧约束') { throw "the old 约束 body should be replaced" }
    Remove-Item -Recurse -Force $proj
}

# Test 13: dry-run must not invent a mapping for unmatched sections
Run-Test "dry-run does not invent mapping" {
    $proj = Setup-Project
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    $out = & $SeleBin --root $proj close $id --dry-run --output json | ConvertFrom-Json
    if (-not $out.suggested_mapping) { throw "suggested_mapping missing" }
    foreach ($entry in $out.suggested_mapping.PSObject.Properties) {
        if ($entry.Value -ne "") { throw "unmatched section $($entry.Name) should map to empty, got '$($entry.Value)'" }
    }
    Remove-Item -Recurse -Force $proj
}

# Test 14: closing a non-active change leaves the active pointer alone
Run-Test "close leaves foreign active pointer" {
    $proj = Setup-Project
    $first = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    $second = (& $SeleBin --root $proj new --slug other-change --output json | ConvertFrom-Json).id
    $out = & $SeleBin --root $proj close $first --output json | ConvertFrom-Json
    if ($out.state_updated) { throw "state should not be updated when the pointer differs" }
    $state = Get-Content "$proj/.agent/STATE.md" -Raw
    if ($state -notmatch [regex]::Escape($second)) { throw "STATE.md should still point to $second" }
    Remove-Item -Recurse -Force $proj
}

# Test 11: status text output (default format must not be silent)
Run-Test "status text output" {
    $proj = Setup-Project
    $id = (& $SeleBin --root $proj new --slug demo-change --output json | ConvertFrom-Json).id
    $text = (& $SeleBin --root $proj status) -join "`n"
    if ([string]::IsNullOrWhiteSpace($text)) { throw "status produced no text output" }
    if ($text -notmatch [regex]::Escape($id)) { throw "status text should mention active id: $text" }
    if ($text -notmatch 'Tasks: 0/2 done') { throw "status text should report task counts: $text" }
    if ($text -notmatch 'Consistent: yes') { throw "status text should report consistency: $text" }
    Remove-Item -Recurse -Force $proj
}

# Test 12: every command produces text output in default format
Run-Test "text output for all commands" {
    $fresh = Join-Path ([System.IO.Path]::GetTempPath()) ([System.IO.Path]::GetRandomFileName())
    $text = (& $SeleBin init $fresh) -join "`n"
    if ($text -notmatch '^Initialized ') { throw "init produced no text output: $text" }
    Remove-Item -Recurse -Force $fresh

    $proj = Setup-Project
    $text = (& $SeleBin --root $proj new --slug demo-change) -join "`n"
    if ($text -notmatch '^Created ') { throw "new produced no text output: $text" }
    $id = (& $SeleBin --root $proj status --output json | ConvertFrom-Json).active_id

    $text = (& $SeleBin --root $proj append-decision --context c --decision d --consequences e) -join "`n"
    if ($text -notmatch '^Appended decision #1') { throw "append-decision produced no text output: $text" }

    $text = (& $SeleBin --root $proj lint) -join "`n"
    if ($text -notmatch '^Lint passed') { throw "lint produced no text output: $text" }

    $text = (& $SeleBin --root $proj close $id --dry-run) -join "`n"
    if ($text -notmatch "^Close preview for $id") { throw "close produced no text output: $text" }

    Remove-Item -Recurse -Force $proj
}

# Test 15: version subcommand matches the build version
Run-Test "version command" {
    $json = & $SeleBin version --output json | ConvertFrom-Json
    if ($json.name -ne "sele") { throw "name mismatch: $($json.name)" }
    if ($json.version -notmatch '^\d+\.\d+\.\d+$') { throw "version should be semver, got '$($json.version)'" }
    if (-not $json.cli11) { throw "cli11 version missing" }
    if (-not $json.nlohmann_json) { throw "nlohmann_json version missing" }

    $text = (& $SeleBin version) -join "`n"
    if ($text -notmatch [regex]::Escape("sele $($json.version)")) { throw "text output should mention the version: $text" }

    $flag = (& $SeleBin --version) -join "`n"
    if (-not ($flag -match [regex]::Escape($json.version))) { throw "--version should report the same version: $flag" }
}

Write-Host "=== All tests passed! ===" -ForegroundColor Green