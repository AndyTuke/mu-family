# Extracts widenNonAsciiUtf8 from the patched JUCE header and runs widen_test.cpp against it.
# Run from a Visual Studio "x64 Native Tools" / Developer PowerShell so that cl.exe is on the PATH.
param(
    [Parameter(Mandatory)][string]$JuceRoot,
    [Parameter(Mandatory)][string]$OutDir
)

$ErrorActionPreference = 'Stop'

if (-not (Get-Command cl -ErrorAction SilentlyContinue)) {
    throw 'cl.exe not found. Start this from a Visual Studio Developer PowerShell.'
}

$header = Join-Path $JuceRoot 'modules/juce_audio_plugin_client/VST3/juce_VST3ModuleInfo.h'
$lines  = @(Get-Content -LiteralPath $header)

# Find the function: from its declaration line to the first line that is just "}".
$start = -1
for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -match 'static void widenNonAsciiUtf8') { $start = $i; break }
}
if ($start -lt 0) { throw "widenNonAsciiUtf8 not found in $header. Is the patch applied?" }

$end = -1
for ($i = $start; $i -lt $lines.Count; $i++) {
    if ($lines[$i].TrimEnd() -eq '}') { $end = $i; break }
}
if ($end -lt 0) { throw 'End of widenNonAsciiUtf8 not found.' }

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$lines[$start..$end] | Set-Content -LiteralPath (Join-Path $OutDir 'widen_extracted.inc') -Encoding utf8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'widen_test.cpp') -Destination $OutDir -Force

Push-Location $OutDir
try {
    Write-Host "Extracted lines $($start + 1)-$($end + 1) of the header. Compiling..."
    & cl /nologo /std:c++17 /utf-8 /EHsc /W4 /Fe:widen_test.exe widen_test.cpp
    if ($LASTEXITCODE -ne 0) { throw 'Compile failed.' }

    & .\widen_test.exe
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed.' }
}
finally {
    Pop-Location
}
