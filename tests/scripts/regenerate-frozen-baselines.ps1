# regenerate-frozen-baselines.ps1 - the ONLY way to rewrite the frozen parameter manifests and compat-corpus baselines.
#
# The layer-extraction safety net compares every product against text frozen in the repo:
#   tests/manifests/<product>.txt            every APVTS parameter id, type, range and default
#   tests/corpus/<product>/*.params.txt      load results of each fixture (parameters, re-save hash)
#   tests/corpus/<product>/CROSSLOAD.txt     what other products' files do when offered to this one
#   tests/corpus/<product>/MANIFEST.txt      SHA-256 of every fixture
#   tests/corpus/<product>/FACTORY_STATE.*   a freshly saved factory-state full preset
# A normal test run only compares. When a parameter, range, default or file format is changed ON PURPOSE,
# run this script, read the git diff of tests/manifests and tests/corpus, and commit it together with the
# change that caused it. It is never run automatically; it refuses to run without -Reason.
#
#   pwsh tests/scripts/regenerate-frozen-baselines.ps1 -Reason "added param X to mu-Tant" [-Config Debug] [-Products mu-clid,mu-tant]
#
# The test executables must already be built (cmake --build build --config Debug --target mu-<product>-tests).
param(
    [Parameter(Mandatory = $true)][string]$Reason,
    [string]$Config = 'Debug',
    [string[]]$Products = @('mu-clid', 'mu-tant', 'mu-toni', 'mu-on'),
    [string]$BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'build')
)

$ErrorActionPreference = 'Stop'
Write-Host "Regenerating frozen baselines. Reason: $Reason"

$env:MU_REGENERATE_FROZEN = '1'
try {
    foreach ($p in $Products) {
        $exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "$p-tests.exe" -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -match "\\$Config\\" } | Select-Object -First 1
        if (-not $exe) { throw "No $Config $p-tests.exe under $BuildDir - build it first." }
        Write-Host "== $p ($($exe.FullName))"
        & $exe.FullName "$p-frozen" | Select-String -Pattern 'REGENERATED|failure' | ForEach-Object { $_.Line }
    }
}
finally {
    Remove-Item Env:\MU_REGENERATE_FROZEN -ErrorAction SilentlyContinue
}
Write-Host 'Done. Review "git diff --stat tests/manifests tests/corpus" before committing.'
