# test-check-layer-boundary.ps1 — self-test for check-layer-boundary.ps1.
#
# Each folder under fixtures/layer-boundary/ is a synthetic repo root holding mu-core/ and zero or more
# mu-*-layer/ libraries (the real libraries do not exist yet). fail-* must exit 1 with exactly one
# violation, pass-* must exit 0, so the checker is proven to reject a violating tree and accept a clean one,
# and to cope with a tree that has no layer library at all.
#
#   pwsh tests/scripts/test-check-layer-boundary.ps1
#
# Exit code 0 = every case behaved as expected, 1 = one or more did not.

$ErrorActionPreference = 'Stop'

$checker  = Join-Path $PSScriptRoot 'check-layer-boundary.ps1'
$fixtures = Join-Path $PSScriptRoot 'fixtures/layer-boundary'

$bad = 0
$cases = @(Get-ChildItem -LiteralPath $fixtures -Directory | Sort-Object Name)
foreach ($case in $cases) {
    $want = if ($case.Name -like 'fail-*') { 1 } else { 0 }
    $out  = & pwsh -NoProfile -File $checker -Root $case.FullName 2>&1 | Out-String
    $got  = $LASTEXITCODE
    $countOk = $want -ne 1 -or $out -match '\b1 violation\(s\)'
    if ($got -eq $want -and $countOk) {
        Write-Host "  [ok]   $($case.Name) (exit $got)"
    } else {
        Write-Host "  [FAIL] $($case.Name) — expected exit $want, got $got"
        Write-Host ($out.TrimEnd() -replace '(?m)^', '         ')
        $bad++
    }
}

# A root that does not exist is a setup error (exit 2), not a pass.
$missing = Join-Path ([IO.Path]::GetTempPath()) ("no-such-root-" + [guid]::NewGuid().ToString('N'))
$null = & pwsh -NoProfile -File $checker -Root $missing 2>&1
if ($LASTEXITCODE -eq 2) { Write-Host "  [ok]   missing-root (exit 2)" } else { Write-Host "  [FAIL] missing-root — expected exit 2, got $LASTEXITCODE"; $bad++ }

# The real repo has no layer library yet: the checker must pass on it.
$null = & pwsh -NoProfile -File $checker -Quiet 2>&1
if ($LASTEXITCODE -eq 0) { Write-Host "  [ok]   real-repo (exit 0)" } else { Write-Host "  [FAIL] real-repo — expected exit 0, got $LASTEXITCODE"; $bad++ }

$status = if ($bad -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "test-check-layer-boundary: $status ($($cases.Count + 2) cases, $bad failed)"
exit $(if ($bad -eq 0) { 0 } else { 1 })
