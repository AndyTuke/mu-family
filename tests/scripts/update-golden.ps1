# update-golden.ps1 - the ONLY way a golden render changes (record a new one or replace one).
#
# A golden (tests/golden/<product>/<scenario>/golden.<config>.txt) is the fingerprint of a scenario's
# render, written by mu-core/Tests/GoldenRender.h. A code-moving change must leave every golden alone;
# a deliberate behaviour change updates it here, in a commit that touches goldens and nothing else.
#
#   pwsh tests/scripts/update-golden.ps1 -Product mu-toni -Scenario factory -Reason "why the sound is meant to change"
#   pwsh tests/scripts/update-golden.ps1 -Product mu-toni -Scenario factory -Reason "..." -TestExe path\to\mu-toni-tests.exe
#
# What it does, in order (any failed step stops it and writes nothing):
#   1. needs -Reason (what changed and why it is correct; the dsp-controller signs it before the commit);
#   2. refuses on a dirty working tree, so the golden change can never ride along with a source change;
#   3. runs the product's test exe twice in update mode for ONE scenario (there is no update-all), each in
#      its own process and its own temp folder, and requires the two files to be identical: the
#      cross-process repeat test of the harness (a clock-seeded generator fails here);
#   4. prints how many lines differ from the golden already in git (first diverging segment first);
#   5. refuses if running the tests changed anything tracked outside tests/golden;
#   6. copies the file into tests/golden/<product>/<scenario>/ and writes a commit-message template to
#      tests/_out/golden/commit-msg.txt. It never stages or commits.
#
# Rules it exists to enforce (CLAUDE.md, tests.md "Layer extraction safety net"):
#   - never run it inside a code-moving commit: commit the code first, run this on a clean tree, commit
#     the goldens alone with the template, then continue;
#   - a stage never changes a golden in the same commit as the code move;
#   - never update a golden just to turn a run green.
#
# Exit code 0 = golden recorded (uncommitted); 1 = refused or failed.

param(
    [Parameter(Mandatory = $true)][string]$Product,
    [Parameter(Mandatory = $true)][string]$Scenario,
    [Parameter(Mandatory = $true)][string]$Reason,
    [string]$TestExe,
    [string]$Config = 'windows-x64-debug'   # must match the config the exe was built as (the harness checks)
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Set-Location $repoRoot

function Refuse([string]$msg) { Write-Host "update-golden: REFUSED - $msg"; exit 1 }

if ([string]::IsNullOrWhiteSpace($Reason)) { Refuse 'a non-empty -Reason is required' }
if ($Product -notmatch '^[A-Za-z0-9._-]+$' -or $Scenario -notmatch '^[A-Za-z0-9._-]+$') { Refuse 'product and scenario must be plain names' }

# Step 2: a dirty tree could hide a source change inside the golden commit.
$dirty = git status --porcelain
if ($dirty) { Refuse "the working tree is not clean; commit or revert first:`n$dirty" }

# Locate the test exe: explicit, else the newest <product>-tests.exe under build/ for this configuration.
if (-not $TestExe) {
    $mode = if ($Config -like '*-release') { 'Release' } else { 'Debug' }
    $found = Get-ChildItem -Path (Join-Path $repoRoot 'build') -Recurse -Filter "$Product-tests.exe" -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match "[\\/]$mode[\\/]" } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $found) { Refuse "no $Product-tests.exe found under build/ for $mode; build it or pass -TestExe" }
    $TestExe = $found.FullName
}
if (-not (Test-Path -LiteralPath $TestExe)) { Refuse "test exe not found: $TestExe" }

$baseline = (git rev-parse --short HEAD).Trim()
$buildNo = if (Test-Path build_number.txt) { (Get-Content build_number.txt -Raw).Trim() } else { 'unknown' }
$fileName = "golden.$Config.txt"
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("mu-golden-" + [guid]::NewGuid().ToString('N'))

# Step 3: two independent processes, each recording into its own root.
$runs = @()
foreach ($n in 1, 2) {
    $root = Join-Path $work "run$n"
    New-Item -ItemType Directory -Force $root | Out-Null
    $env:MU_GOLDEN_ROOT = $root
    $env:MU_GOLDEN_UPDATE = '1'
    $env:MU_GOLDEN_SCENARIO = "$Product/$Scenario"
    $env:MU_GOLDEN_REASON = $Reason
    $env:MU_GOLDEN_BASELINE = "$baseline build $buildNo"
    $env:MU_GOLDEN_EXPECT_CONFIG = $Config
    & $TestExe | Out-Host
    $code = $LASTEXITCODE
    Remove-Item Env:MU_GOLDEN_ROOT, Env:MU_GOLDEN_UPDATE, Env:MU_GOLDEN_SCENARIO, Env:MU_GOLDEN_REASON, Env:MU_GOLDEN_BASELINE, Env:MU_GOLDEN_EXPECT_CONFIG -ErrorAction SilentlyContinue
    if ($code -ne 0) { Remove-Item -Recurse -Force $work; Refuse "run $n of the test exe failed (exit $code)" }
    $out = Join-Path $root "$Product/$Scenario/$fileName"
    if (-not (Test-Path -LiteralPath $out)) { Remove-Item -Recurse -Force $work; Refuse "run $n wrote no golden at $Product/$Scenario/$fileName (wrong product/scenario/config name?)" }
    $runs += $out
}

# Compare ignoring the meta.* lines (reason/baseline), which are the same anyway.
function Body([string]$path) { Get-Content -LiteralPath $path | Where-Object { $_ -notmatch '^meta\.' } }
$diffRepeat = Compare-Object (Body $runs[0]) (Body $runs[1])
if ($diffRepeat) {
    Write-Host ($diffRepeat | Select-Object -First 10 | Out-String)
    Remove-Item -Recurse -Force $work
    Refuse 'the scenario is not repeatable across two processes; fix the nondeterminism (seed, uninitialised memory), do not record it'
}

# Step 4: how far the new golden is from the committed one.
$target = Join-Path $repoRoot "tests/golden/$Product/$Scenario/$fileName"
if (Test-Path -LiteralPath $target) {
    $d = Compare-Object (Body $target) (Body $runs[0])
    if (-not $d) { Write-Host 'update-golden: the golden is already identical; nothing to change.'; Remove-Item -Recurse -Force $work; exit 0 }
    Write-Host "update-golden: $(@($d).Count) line(s) differ from the committed golden; first ones:"
    $d | Where-Object { $_.SideIndicator -eq '=>' } | Select-Object -First 10 | ForEach-Object { Write-Host "  new: $($_.InputObject)" }
} else {
    Write-Host 'update-golden: first recording for this scenario/config.'
}

# Step 5: the test run must not have touched any tracked file.
$after = git status --porcelain
if ($after) { Remove-Item -Recurse -Force $work; Refuse "running the tests changed files:`n$after" }

# Step 6: install, verify only goldens changed, write the commit template.
New-Item -ItemType Directory -Force (Split-Path -Parent $target) | Out-Null
Copy-Item -LiteralPath $runs[0] -Destination $target -Force
Remove-Item -Recurse -Force $work

$changed = git status --porcelain
$outside = @($changed | Where-Object { $_ -notmatch ' "?tests/golden/' })
if ($outside.Count -gt 0) { Refuse "it would also change files outside tests/golden:`n$($outside -join "`n")" }

$msgDir = Join-Path $repoRoot 'tests/_out/golden'
New-Item -ItemType Directory -Force $msgDir | Out-Null
$template = @"
Goldens: $Product/$Scenario ($Config) - <what behaviour changed>

Reason: $Reason
Baseline: $baseline (build $buildNo)

This commit changes goldens only. dsp-controller sign-off on the reason: <pending>

Refs #<issue>
Version: v<major.minor>.$buildNo
"@
Set-Content -LiteralPath (Join-Path $msgDir 'commit-msg.txt') -Value $template -Encoding utf8

Write-Host "update-golden: recorded tests/golden/$Product/$Scenario/$fileName (not staged, not committed)."
Write-Host "Next: dsp-controller signs the reason, then 'git add tests/golden' and commit with tests/_out/golden/commit-msg.txt as a goldens-only commit."
exit 0
