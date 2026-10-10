# test-check-comment-refs.ps1 — self-test for check-comment-refs.ps1 against known fixtures.
#
# Each folder under fixtures/comment-refs/ is one case; its name prefix gives the expected
# result: fail-* = exit 1 (one violation), pass-* = exit 0, setup-* = exit 2 (nothing to scan).
# Two cases are made at runtime because they cannot live in git: a nested build/ folder (git
# ignores build/) and a completely empty folder.
#
#   pwsh tests/scripts/test-check-comment-refs.ps1
#
# Exit code 0 = every case behaved as expected, 1 = one or more cases did not.

$ErrorActionPreference = 'Stop'

$checker  = Join-Path $PSScriptRoot 'check-comment-refs.ps1'
$fixtures = Join-Path $PSScriptRoot 'fixtures/comment-refs'

# Build the runtime-only cases in a temp folder.
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("comment-refs-" + [guid]::NewGuid().ToString('N'))
$nested = Join-Path $tmp 'pass-nested-build'
New-Item -ItemType Directory -Force (Join-Path $nested 'sub/build') | Out-Null
Copy-Item (Join-Path $fixtures 'pass-nested-build/*') $nested
[IO.File]::WriteAllText((Join-Path $nested 'sub/build/gen.cpp'), "int y = 2; // #500 generated, must be skipped`n")
$empty = Join-Path $tmp 'setup-empty'
New-Item -ItemType Directory -Force $empty | Out-Null

$cases = @(Get-ChildItem -LiteralPath $fixtures -Directory | Where-Object Name -ne 'pass-nested-build' | ForEach-Object FullName) + @($nested, $empty)

# Run the checker on every case and compare its exit code with the one the folder name promises.
$bad = 0
try {
    foreach ($case in $cases) {
        $name = Split-Path -Leaf $case
        $want = if ($name -like 'fail-*') { 1 } elseif ($name -like 'setup-*') { 2 } else { 0 }
        $out  = & pwsh -NoProfile -File $checker -Path $case 2>&1 | Out-String
        $got  = $LASTEXITCODE
        # A fail case must report exactly one violation, so a stray extra hit is caught too.
        $countOk = $want -ne 1 -or $out -match '\b1 violation\(s\)'
        if ($got -eq $want -and $countOk) {
            Write-Host "  [ok]   $name (exit $got)"
        } else {
            Write-Host "  [FAIL] $name — expected exit $want, got $got"
            Write-Host ($out.TrimEnd() -replace '(?m)^', '         ')
            $bad++
        }
    }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

$status = if ($bad -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "test-check-comment-refs: $status ($($cases.Count) cases, $bad failed)"
exit $(if ($bad -eq 0) { 0 } else { 1 })
