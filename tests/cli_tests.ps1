#!/usr/bin/env pwsh

param(
    [Parameter(Mandatory=$true)]
    [string]$DesignBin
)

if (-not (Test-Path $DesignBin)) {
    Write-Error "Usage: $0 <path-to-design-binary>"
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
    $out = & $DesignBin --root $proj new --slug demo-change --output json | ConvertFrom-Json
    if ($out.id -ne "20261018-demo-change") { throw "ID mismatch" }
    if (-not (Test-Path "$proj/.design/changes/20261018-demo-change")) { throw "changes dir missing" }
    if (-not (Test-Path "$proj/.agent/work/20261018-demo-change")) { throw "work dir missing" }
    if (-not (Test-Path "$proj/.design/changes/20261018-demo-change/design.md")) { throw "design.md missing" }
    if (-not (Test-Path "$proj/.agent/work/20261018-demo-change/tasks.md")) { throw "tasks.md missing" }
    $state = Get-Content "$proj/.agent/STATE.md" -Raw
    if ($state -notmatch "Active Change: 20261018-demo-change") { throw "STATE.md not updated" }
    Remove-Item -Recurse -Force $proj
}

# Test 2: new duplicate ID
Run-Test "new duplicate ID" {
    $proj = Setup-Project
    & $DesignBin --root $proj new --slug demo-change --output json | Out-Null
    $out = & $DesignBin --root $proj new --slug demo-change --output json 2>&1 | ConvertFrom-Json
    if (-not $out.error) { throw "Expected error for duplicate ID" }
    Remove-Item -Recurse -Force $proj
}

# Test 3: new invalid slug
Run-Test "new invalid slug" {
    $proj = Setup-Project
    $out = & $DesignBin --root $proj new --slug "Invalid_Slug" --output json 2>&1 | ConvertFrom-Json
    if (-not $out.error) { throw "Expected error for invalid slug" }
    if ($out.error -notmatch "lowercase") { throw "Error message should mention lowercase" }
    Remove-Item -Recurse -Force $proj
}

# Test 4: append-decision success
Run-Test "append-decision success" {
    $proj = Setup-Project
    & $DesignBin --root $proj new --slug demo-change --output json | Out-Null
    & $DesignBin --root $proj append-decision --context "Test context" --decision "Test decision" --consequences "Test consequences" --change-id 20261018-demo-change --output json | Out-Null
    & $DesignBin --root $proj append-decision --context "Test context 2" --decision "Test decision 2" --consequences "Test consequences 2" --output json | Out-Null
    $count = (Get-Content "$proj/.design/DECISIONS.log" | Where-Object { $_ -match '^## \[' }).Count
    if ($count -ne 2) { throw "Expected 2 decisions, got $count" }
    Remove-Item -Recurse -Force $proj
}

# Test 5: append-decision missing params
Run-Test "append-decision missing params" {
    $proj = Setup-Project
    $out = & $DesignBin --root $proj append-decision --context "c" --decision "d" --output json 2>&1 | ConvertFrom-Json
    if (-not $out.error) { throw "Expected error for missing consequences" }
    Remove-Item -Recurse -Force $proj
}

# Test 6: status JSON output
Run-Test "status JSON output" {
    $proj = Setup-Project
    & $DesignBin --root $proj new --slug demo-change --output json | Out-Null
    # Mark first task as done
    $tasks = Get-Content "$proj/.agent/work/20261018-demo-change/tasks.md"
    $tasks = $tasks -replace '- \[ \] 步骤 1', '- [x] 步骤 1'
    $tasks | Set-Content "$proj/.agent/work/20261018-demo-change/tasks.md"
    $out = & $DesignBin --root $proj status --output json | ConvertFrom-Json
    if ($out.active_id -ne "20261018-demo-change") { throw "Active ID mismatch" }
    if ($out.tasks.total -ne 2) { throw "Expected total=2, got $($out.tasks.total)" }
    if ($out.tasks.done -ne 1) { throw "Expected done=1, got $($out.tasks.done)" }
    Remove-Item -Recurse -Force $proj
}

# Test 7: lint ID inconsistency
Run-Test "lint ID inconsistency" {
    $proj = Setup-Project
    & $DesignBin --root $proj new --slug demo-change --output json | Out-Null
    Remove-Item -Recurse -Force "$proj/.agent/work/20261018-demo-change"
    $out = & $DesignBin --root $proj lint --output json 2>&1 | ConvertFrom-Json
    if ($out.passed) { throw "Expected lint to fail" }
    $check = $out.checks | Where-Object { $_.name -eq "id_consistency" }
    if ($check.passed) { throw "id_consistency should fail" }
    Remove-Item -Recurse -Force $proj
}

# Test 8: lint SPEC.md process words
Run-Test "lint SPEC.md process words" {
    $proj = Setup-Project
    "TODO: fix this" | Add-Content "$proj/.design/SPEC.md"
    $out = & $DesignBin --root $proj lint --output json 2>&1 | ConvertFrom-Json
    if ($out.passed) { throw "Expected lint to fail" }
    $check = $out.checks | Where-Object { $_.name -eq "spec_process_words" }
    if ($check.passed) { throw "spec_process_words should fail" }
    Remove-Item -Recurse -Force $proj
}

# Test 9: close dry-run
Run-Test "close dry-run" {
    $proj = Setup-Project
    & $DesignBin --root $proj new --slug demo-change --output json | Out-Null
    $out = & $DesignBin --root $proj close 20261018-demo-change --dry-run --output json | ConvertFrom-Json
    if (-not $out.dry_run) { throw "dry_run should be true" }
    if ($out.id -ne "20261018-demo-change") { throw "ID mismatch" }
    # Verify no files modified
    $mtime1 = (Get-Item "$proj/.design/changes/20261018-demo-change/design.md").LastWriteTime
    $mtime2 = (Get-Item "$proj/.agent/work/20261018-demo-change/tasks.md").LastWriteTime
    Start-Sleep -Seconds 1
    & $DesignBin --root $proj close 20261018-demo-change --dry-run --output json | Out-Null
    $mtime3 = (Get-Item "$proj/.design/changes/20261018-demo-change/design.md").LastWriteTime
    $mtime4 = (Get-Item "$proj/.agent/work/20261018-demo-change/tasks.md").LastWriteTime
    if ($mtime1 -ne $mtime3) { throw "design.md was modified" }
    if ($mtime2 -ne $mtime4) { throw "tasks.md was modified" }
    Remove-Item -Recurse -Force $proj
}

# Test 10: close without dry-run
Run-Test "close without dry-run" {
    $proj = Setup-Project
    & $DesignBin --root $proj new --slug demo-change --output json | Out-Null
    $out = & $DesignBin --root $proj close 20261018-demo-change --output json 2>&1 | ConvertFrom-Json
    if (-not $out.error) { throw "Expected error for close without dry-run" }
    if ($out.error -notmatch "MVP") { throw "Error should mention MVP" }
    Remove-Item -Recurse -Force $proj
}

Write-Host "=== All tests passed! ===" -ForegroundColor Green