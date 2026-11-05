# Builds JUCE's own example plugin as a VST3 named with a Greek mu, then reads the class names
# out of the generated moduleinfo.json. Run once on unpatched develop (expect FAIL) and once on
# the patched branch (expect PASS). Run from a Visual Studio Developer PowerShell.
param(
    [Parameter(Mandatory)][string]$JuceRoot,   # the JUCE checkout to test
    [Parameter(Mandatory)][string]$WorkDir,    # scratch folder, not on C:
    [Parameter(Mandatory)][string]$Label,      # e.g. unpatched / patched, keeps the two builds apart
    [switch]$Utf8                              # compile with /utf-8, as a real project that names its plugin non-ASCII must (without it MSVC turns the mu into '?' before JUCE sees it)
)

$ErrorActionPreference = 'Stop'

$mu       = [string][char]0x03BC
$expected = "$mu-Test"

$src  = Join-Path $JuceRoot 'examples/CMake/AudioPlugin'
$proj = Join-Path $WorkDir "proj-$Label"
if (Test-Path $proj) { Remove-Item -Recurse -Force $proj }
Copy-Item -Recurse -LiteralPath $src -Destination $proj

# Edit the copy only: use the JUCE checkout, build just the VST3, and give it a PLUGIN_NAME with a mu.
$cmake   = Join-Path $proj 'CMakeLists.txt'
$text    = [IO.File]::ReadAllText($cmake)
$juceFwd = $JuceRoot -replace '\\', '/'
$text = $text -replace '# add_subdirectory\(JUCE\)[^\r\n]*', "add_subdirectory(`"$juceFwd`" juce-build)"
$text = $text -replace 'FORMATS AU VST3 Standalone', 'FORMATS VST3'
$text = $text -replace '(PRODUCT_NAME "Audio Plugin Example")', ('$1' + "`n    PLUGIN_NAME `"$expected`"")
[IO.File]::WriteAllText($cmake, $text, [Text.UTF8Encoding]::new($false))

$build = Join-Path $proj 'build'
if ($Utf8) { $env:CXXFLAGS = '/utf-8' } else { Remove-Item Env:CXXFLAGS -ErrorAction SilentlyContinue }
& cmake -S $proj -B $build
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

& cmake --build $build --config Release --target AudioPluginExample_VST3
if ($LASTEXITCODE -ne 0) { throw 'Build failed.' }

$info = Get-ChildItem -Recurse -Filter moduleinfo.json -Path $build | Select-Object -First 1
if (-not $info) { throw 'No moduleinfo.json was generated.' }
Write-Host "moduleinfo.json: $($info.FullName)"

$json = Get-Content -Raw -Encoding utf8 -LiteralPath $info.FullName | ConvertFrom-Json

$allPass = $true
foreach ($class in $json.Classes) {
    $name  = [string]$class.Name
    $codes = ($name.ToCharArray() | ForEach-Object { '{0:X4}' -f [int]$_ }) -join ' '
    $pass  = $name -ceq $expected
    if (-not $pass) { $allPass = $false }
    Write-Host ("{0}  class '{1}'  code points: {2}" -f ($(if ($pass) { 'PASS' } else { 'FAIL' }), $name, $codes))
}

Write-Host ("Expected '{0}' = code points 03BC 002D 0054 0065 0073 0074" -f $expected)
if ($allPass) { Write-Host "[$Label] all class names correct" } else { Write-Host "[$Label] garbled class names found" }
