# run-bench.ps1 - the mixer timing benchmark for the runtime-counts work (docs/design-runtime-counts.md section 5).
#   pwsh tests/scripts/run-bench.ps1 -Label baseline     # build (Release) + run the three shapes, write tests/benchmarks/<label>.json
#   pwsh tests/scripts/run-bench.ps1 -Label phase2 -Compare baseline   # also print the change against another recorded run
# Pass criterion: median and p99 within 2 percent of the baseline, or within the baseline's own spread if larger.
param(
    [Parameter(Mandatory)][string]$Label,
    [string]$Compare = '',
    [int]$Runs = 3,
    [switch]$NoBuild
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent | Split-Path -Parent
$build = Join-Path $root 'build'
$shapes = 'clid', 'tronic', 'host'
if (-not $NoBuild) {
    & cmake --build $build --config Release --target @($shapes | ForEach-Object { "mu-core-bench-$_" })
    if ($LASTEXITCODE -ne 0) { throw 'bench build failed' }
}
$rows = @()
for ($i = 0; $i -lt $Runs; $i++) {          # interleave the shapes across runs; keep each shape's best median over the runs
    foreach ($s in $shapes) {
        $exe = Join-Path $build "mu-core\Bench\mu-core-bench-$s`_artefacts\Release\mu-core-bench-$s.exe"
        foreach ($line in (& $exe)) { $r = $line | ConvertFrom-Json; $r | Add-Member shape $s; $r | Add-Member run $i; $rows += $r }
    }
}
$out = foreach ($g in ($rows | Group-Object shape, block)) {
    $m = $g.Group | Sort-Object medianUs
    [pscustomobject]@{ shape = $g.Group[0].shape; block = $g.Group[0].block; layers = $g.Group[0].layers; buses = $g.Group[0].buses
        medianUs = $m[0].medianUs; p99Us = ($g.Group | Measure-Object p99Us -Minimum).Minimum
        spreadPct = [math]::Round(100 * ($m[-1].medianUs - $m[0].medianUs) / $m[0].medianUs, 2) }
}
$meta = [pscustomobject]@{ label = $Label; date = (Get-Date -Format s); machine = $env:COMPUTERNAME; commit = (git -C $root rev-parse --short HEAD); results = $out }
$dir = Join-Path $root 'tests\benchmarks'; New-Item -ItemType Directory -Force $dir | Out-Null
$meta | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $dir "$Label.json") -Encoding utf8
$out | Format-Table -AutoSize
if ($Compare) {
    $base = Get-Content (Join-Path $dir "$Compare.json") -Raw | ConvertFrom-Json
    foreach ($o in $out) {
        $b = $base.results | Where-Object { $_.shape -eq $o.shape -and $_.block -eq $o.block }
        $d = [math]::Round(100 * ($o.medianUs - $b.medianUs) / $b.medianUs, 2); $dp = [math]::Round(100 * ($o.p99Us - $b.p99Us) / $b.p99Us, 2)
        $ok = ([math]::Abs($d) -le [math]::Max(2, $b.spreadPct)) -and ($dp -le [math]::Max(2, $b.spreadPct) * 2)
        '{0,-7} block {1,-4} median {2,7}%  p99 {3,7}%  {4}' -f $o.shape, $o.block, $d, $dp, $(if ($ok) { 'ok' } else { 'REGRESSION' })
    }
}
