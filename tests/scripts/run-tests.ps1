# run-tests.ps1 - build the unit-test targets and run every suite IN PARALLEL (the slow ones, mu-Tant and mu-Toni, start first).
#
# Each suite is one single-threaded console app, so running them side by side on a many-core PC costs about as long as the slowest one
# instead of the sum of all (a Debug sweep that took ~45 minutes in series takes ~20). One summary line per suite, with its time;
# the full log of each is kept in <BuildDir>/test-logs/<suite>.log. Exit code 1 if any suite fails or times out.
#
#   pwsh tests/scripts/run-tests.ps1                     # build + run all
#   pwsh tests/scripts/run-tests.ps1 -NoBuild            # run what is built
#   pwsh tests/scripts/run-tests.ps1 -Suites mu-on,mu-clid
#   pwsh tests/scripts/run-tests.ps1 -Fast               # skip the two slow suites (mu-tant, mu-toni) for a quick check
#   pwsh tests/scripts/run-tests.ps1 -Goldens            # also run the golden renders (mu-clid, mu-on, mu-tant, mu-toni): slow, end of day / before a release
#
# By default a product's test app skips its golden renders (categories ending "-golden"); see mu-core/Tests/TestSelection.h.
param(
    [string[]]$Suites = @('mu-tant', 'mu-toni', 'mu-core-tests', 'mu-core-tests-16', 'mu-core-tests-bus1', 'mu-clid', 'mu-on', 'mu-tilate', 'mu-tronic', 'mu-host', 'mu-control'),
    [string]$Config = 'Debug',
    [string]$BuildDir = (Join-Path (Split-Path $PSScriptRoot -Parent | Split-Path -Parent) 'build'),
    [int]$TimeoutMin = 45,
    [switch]$NoBuild,
    [switch]$Fast,
    [switch]$Goldens
)
$ErrorActionPreference = 'Stop'
if ($Fast) { $Suites = $Suites | Where-Object { $_ -notin 'mu-tant', 'mu-toni' } }

# A suite name is the product ("mu-on" -> target mu-on-tests) or the full target (mu-core-tests-16).
function Get-Target([string]$s) { if ($s -like '*-tests*') { $s } else { "$s-tests" } }
$targets = $Suites | ForEach-Object { Get-Target $_ }

if (-not $NoBuild) {
    Write-Host "Building: $($targets -join ' ')"
    & cmake --build $BuildDir --config $Config --target @targets -- /m
    if ($LASTEXITCODE -ne 0) { throw 'build failed' }
}

$logDir = Join-Path $BuildDir 'test-logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
# Suites side by side must not share a temp folder: several tests write fixed names there (mu-core-loader-tests, mu-clip-launch-test, ...),
# and the three mu-core variants run the same tests. Each suite gets its own TMP/TEMP (JUCE's tempDirectory reads it).
$tmpRoot = Join-Path $BuildDir 'test-tmp'
if (Test-Path $tmpRoot) { Remove-Item -Recurse -Force $tmpRoot -ErrorAction SilentlyContinue }
function Start-Suite([string]$name, [string]$exe, [string[]]$arguments, [string]$log) {
    $tmp = Join-Path $tmpRoot $name
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $savedTmp, $savedTemp = $env:TMP, $env:TEMP
    $env:TMP = $tmp; $env:TEMP = $tmp
    try {
        $splat = @{ FilePath = $exe; WorkingDirectory = (Split-Path $exe); RedirectStandardOutput = $log; RedirectStandardError = "$log.err"; PassThru = $true; NoNewWindow = $true }
        if ($arguments) { $splat.ArgumentList = $arguments }
        Start-Process @splat
    } finally { $env:TMP = $savedTmp; $env:TEMP = $savedTemp }
}
$running = @()
foreach ($s in $Suites) {
    $t = Get-Target $s
    $exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "$t.exe" -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -match "\\$Config\\" } | Select-Object -First 1
    if ($null -eq $exe) { Write-Host "$s : no $t.exe built for $Config"; $running += [pscustomobject]@{ Suite = $s; Proc = $null; Log = $null; Start = Get-Date }; continue }
    $log = Join-Path $logDir "$s.log"
    $p = Start-Suite $s $exe.FullName @() $log
    $running += [pscustomobject]@{ Suite = $s; Proc = $p; Log = $log; Start = Get-Date }
    if ($Goldens -and $s -in 'mu-clid', 'mu-on', 'mu-tant', 'mu-toni') {
        $glog = Join-Path $logDir "$s-goldens.log"
        $gp = Start-Suite "$s-goldens" $exe.FullName @('--goldens') $glog
        $running += [pscustomobject]@{ Suite = "$s-goldens"; Proc = $gp; Log = $glog; Start = Get-Date }
    }
}

$deadline = (Get-Date).AddMinutes($TimeoutMin)
$failed = $false
foreach ($r in $running) {
    if ($null -eq $r.Proc) { $failed = $true; continue }
    $left = [int][math]::Max(1, ($deadline - (Get-Date)).TotalSeconds)
    if (-not $r.Proc.WaitForExit($left * 1000)) { $r.Proc.Kill(); $note = 'TIMEOUT'; $failed = $true }
    else { $note = '' }
    $mins = [math]::Round((($(if ($r.Proc.HasExited) { $r.Proc.ExitTime } else { Get-Date })) - $r.Proc.StartTime).TotalMinutes, 1)   # the suite's own run time
    $summary = (Select-String -Path $r.Log -Pattern '^=== ' -ErrorAction SilentlyContinue | Select-Object -Last 1).Line
    if (-not $summary) { $summary = '(no summary line: see ' + $r.Log + ')'; $failed = $true }
    elseif ($summary -notmatch ' 0 failure') { $failed = $true }
    '{0,-20} {1,5} min  {2} {3}' -f $r.Suite, $mins, $summary, $note
}
if ($failed) { Write-Host 'run-tests: FAIL'; exit 1 } else { Write-Host 'run-tests: PASS' }
