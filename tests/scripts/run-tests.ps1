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
#   pwsh tests/scripts/run-tests.ps1 -NoSplit            # one process per suite (no per-category split)
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
    [switch]$Goldens,
    [int]$MaxParallel = 12,                 # processes at once (the PC has 16 logical cores; more makes timing-sensitive tests fail)
    [switch]$NoSplit,                       # run each suite as one process instead of one per test category
    [string[]]$SplitSuites = @('mu-tant', 'mu-toni', 'mu-tronic')   # the slow suites, cut by category and run in parallel
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
# The three mu-core variants run the same tests, and several write fixed names in the temp folder (mu-core-loader-tests, mu-clip-launch-test, ...),
# so side by side they collide. Each mu-core variant gets its own TMP/TEMP (JUCE's tempDirectory reads it).
$tmpRoot = Join-Path $BuildDir 'test-tmp'
if (Test-Path $tmpRoot) { Remove-Item -Recurse -Force $tmpRoot -ErrorAction SilentlyContinue }
function Start-Suite([string]$name, [string]$exe, [string[]]$arguments, [string]$log) {
    $tmp = Join-Path $tmpRoot $name
    New-Item -ItemType Directory -Force $tmp | Out-Null
    $savedTmp, $savedTemp = $env:TMP, $env:TEMP
    if ($name -like 'mu-core-tests*') { $env:TMP = $tmp; $env:TEMP = $tmp }   # only these share fixed temp names; a product's frozen corpus captures paths, so it keeps the real TEMP
    try {
        $splat = @{ FilePath = $exe; WorkingDirectory = (Split-Path $exe); RedirectStandardOutput = $log; RedirectStandardError = "$log.err"; PassThru = $true; NoNewWindow = $true }
        if ($arguments) { $splat.ArgumentList = $arguments }
        Start-Process @splat
    } finally { $env:TMP = $savedTmp; $env:TEMP = $savedTemp }
}
$running = @()   # one entry per process: Suite (the group it reports under), Part (its category, if the suite is split), Proc, Log
function Add-Run([string]$suite, [string]$part, [string]$exe, [string[]]$arguments, [string]$logName) {
    while (@($script:running | Where-Object { $_.Proc -and -not $_.Proc.HasExited }).Count -ge $MaxParallel) { Start-Sleep -Milliseconds 500 }
    $log = Join-Path $logDir "$logName.log"
    $script:running += [pscustomobject]@{ Suite = $suite; Part = $part; Proc = (Start-Suite $logName $exe $arguments $log); Log = $log }
}
foreach ($s in $Suites) {
    $t = Get-Target $s
    $exe = Get-ChildItem -Path $BuildDir -Recurse -Filter "$t.exe" -ErrorAction SilentlyContinue |
           Where-Object { $_.FullName -match "\\$Config\\" } | Select-Object -First 1
    if ($null -eq $exe) { Write-Host "$s : no $t.exe built for $Config"; $running += [pscustomobject]@{ Suite = $s; Part = ''; Proc = $null; Log = $null }; continue }
    if ($s -in $SplitSuites -and -not $NoSplit) {
        # A slow suite is cut by test category and each category runs as its own process: the suite takes as long as its slowest category.
        $listLog = Join-Path $logDir "$s.list.log"
        $lp = Start-Suite "$s-list" $exe.FullName @('--list') $listLog
        $lp.WaitForExit()
        $cats = Get-Content $listLog | Where-Object { $_ -match '^[A-Za-z0-9_-]+$' -and $_ -notmatch '-golden$' }   # category names only (the app also prints its slowest-tests footer)
        foreach ($c in $cats) { Add-Run $s $c $exe.FullName @($c) "$s.$c" }
    } else {
        Add-Run $s '' $exe.FullName @() $s
    }
    if ($Goldens -and $s -in 'mu-clid', 'mu-on', 'mu-tant', 'mu-toni') { Add-Run "$s-goldens" '' $exe.FullName @('--goldens') "$s-goldens" }
}

$deadline = (Get-Date).AddMinutes($TimeoutMin)
$failed = $false
foreach ($g in ($running | Group-Object Suite)) {
    $failures = 0; $results = 0; $maxMins = 0.0; $note = ''; $broken = ''
    foreach ($r in $g.Group) {
        if ($null -eq $r.Proc) { $failed = $true; $broken = 'not built'; continue }
        $left = [int][math]::Max(1, ($deadline - (Get-Date)).TotalSeconds)
        if (-not $r.Proc.WaitForExit($left * 1000)) { $r.Proc.Kill(); $note = 'TIMEOUT'; $failed = $true }
        $end = if ($r.Proc.HasExited) { $r.Proc.ExitTime } else { Get-Date }
        $maxMins = [math]::Max($maxMins, ($end - $r.Proc.StartTime).TotalMinutes)
        $line = (Select-String -Path $r.Log -Pattern '^=== ' -ErrorAction SilentlyContinue | Select-Object -Last 1).Line
        if ($line -match '(\d+) failure\(s\) across (\d+) test result') { $failures += [int]$Matches[1]; $results += [int]$Matches[2] }
        else { $failed = $true; $broken = "no summary line: see $($r.Log)" }
    }
    if ($failures -gt 0) { $failed = $true }
    '{0,-22} {1,5} min  {2} failure(s) across {3} result(s) {4}{5}' -f $g.Name, [math]::Round($maxMins, 1), $failures, $results, $note, $broken
}
if ($failed) { Write-Host 'run-tests: FAIL'; exit 1 } else { Write-Host 'run-tests: PASS' }
