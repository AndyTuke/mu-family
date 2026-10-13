# test-check-core-boundary.ps1 — self-test for the chrome-list and mu-control checks in check-core-boundary.ps1.
#
# Each folder under fixtures/core-boundary/ holds a UIChromeSources.cmake and the files it lists;
# fail-* must exit 1 with one violation, pass-* must exit 0.
#
#   pwsh tests/scripts/test-check-core-boundary.ps1
#
# Exit code 0 = every case behaved as expected, 1 = one or more did not.

$ErrorActionPreference = 'Stop'

$checker  = Join-Path $PSScriptRoot 'check-core-boundary.ps1'
$fixtures = Join-Path $PSScriptRoot 'fixtures/core-boundary'
$emptyCore = Join-Path ([IO.Path]::GetTempPath()) ("core-boundary-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $emptyCore | Out-Null

$bad = 0
$cases = @(Get-ChildItem -LiteralPath $fixtures -Directory)
try {
    foreach ($case in $cases) {
        $want = if ($case.Name -like 'fail-*') { 1 } else { 0 }
        # A case holds a chrome list, a fixture mu-control/ tree, a fixture mu-core/ tree, or a mix. An empty -Path
        # keeps the real mu-core scan out of the way.
        $list = Join-Path $case.FullName 'UIChromeSources.cmake'
        $args = @('-NoProfile', '-File', $checker, '-Path', $emptyCore)
        if (Test-Path -LiteralPath (Join-Path $case.FullName 'mu-core')) { $args[4] = Join-Path $case.FullName 'mu-core' }
        if (Test-Path -LiteralPath (Join-Path $case.FullName 'mu-control')) { $args += @('-ControlPath', (Join-Path $case.FullName 'mu-control')) }
        if (Test-Path -LiteralPath $list) { $args += @('-ChromeList', $list) }
        $out  = & pwsh @args 2>&1 | Out-String
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
} finally {
    Remove-Item -LiteralPath $emptyCore -Recurse -Force -ErrorAction SilentlyContinue
}

$status = if ($bad -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "test-check-core-boundary: $status ($($cases.Count) cases, $bad failed)"
exit $(if ($bad -eq 0) { 0 } else { 1 })
