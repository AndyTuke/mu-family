# PostToolUse hook: inject build artifact list into Claude context after cmake --build.
# Only fires when the build succeeded (exitCode 0).
# Writes per-config stamp files so each config tracks its own build number.
param()
$data = $input | ConvertFrom-Json -ErrorAction SilentlyContinue
if (-not $data -or $data.tool_response.exitCode -ne 0) { exit 0 }

$buildRoot = 'D:\Dev\mu\build'
$curNum    = (Get-Content 'D:\Dev\mu\build_number.txt' -Raw -ErrorAction SilentlyContinue).Trim()
$cmd       = $data.tool_input.command

# Write a per-config stamp so we always know which build number each config is at.
if ($cmd -match '--config\s+Debug')   { Set-Content "$buildRoot\debug_version.txt"   $curNum -NoNewline }
if ($cmd -match '--config\s+Release') { Set-Content "$buildRoot\release_version.txt" $curNum -NoNewline }

# Read each config's actual build number (may differ when only one config was built).
$debugNum   = (Get-Content "$buildRoot\debug_version.txt"   -Raw -ErrorAction SilentlyContinue)?.Trim()
$releaseNum = (Get-Content "$buildRoot\release_version.txt" -Raw -ErrorAction SilentlyContinue)?.Trim()

$items = @(
    [pscustomobject]@{ Name='mu-Clid VST3';       Path="$buildRoot\mu-clid\mu-clid_artefacts\{0}\VST3\mu-Clid.vst3"              },
    [pscustomobject]@{ Name='mu-Clid CLAP';       Path="$buildRoot\mu-clid\mu-clid_artefacts\{0}\CLAP\mu-Clid.clap"              },
    [pscustomobject]@{ Name='mu-Clid Standalone'; Path="$buildRoot\mu-clid\mu-clid_artefacts\{0}\Standalone\mu-Clid.exe"         },
    [pscustomobject]@{ Name='mu-Clid Lite VST3';  Path="$buildRoot\mu-clid\mu-clid-lite_artefacts\{0}\VST3\mu-Clid Lite.vst3"   },
    [pscustomobject]@{ Name='mu-Clid Lite CLAP';  Path="$buildRoot\mu-clid\mu-clid-lite_artefacts\{0}\CLAP\mu-Clid Lite.clap"   },
    [pscustomobject]@{ Name='mu-Tant VST3';       Path="$buildRoot\mu-tant\mu-tant_artefacts\{0}\VST3\mu-Tant.vst3"              },
    [pscustomobject]@{ Name='mu-Tant CLAP';       Path="$buildRoot\mu-tant\mu-tant_artefacts\{0}\CLAP\mu-Tant.clap"              },
    [pscustomobject]@{ Name='mu-Tant Standalone'; Path="$buildRoot\mu-tant\mu-tant_artefacts\{0}\Standalone\mu-Tant.exe"         },
    [pscustomobject]@{ Name='mu-Toni VST3';       Path="$buildRoot\mu-toni\mu-toni_artefacts\{0}\VST3\mu-Toni.vst3"              },
    [pscustomobject]@{ Name='mu-Toni CLAP';       Path="$buildRoot\mu-toni\mu-toni_artefacts\{0}\CLAP\mu-Toni.clap"              },
    [pscustomobject]@{ Name='mu-Toni Standalone'; Path="$buildRoot\mu-toni\mu-toni_artefacts\{0}\Standalone\mu-Toni.exe"         }
)

$debugVer   = if ($debugNum)   { "v1.0.$debugNum" }   else { $null }
$releaseVer = if ($releaseNum) { "v1.0.$releaseNum" } else { $null }

$debugLines   = if ($debugVer)   { $items | Where-Object { Test-Path ($_.Path -f 'Debug') }   | ForEach-Object { "- $($_.Name): $debugVer" } }
$releaseLines = if ($releaseVer) { $items | Where-Object { Test-Path ($_.Path -f 'Release') } | ForEach-Object { "- $($_.Name): $releaseVer" } }

if (-not $debugLines -and -not $releaseLines) { exit 0 }

$lines = @('## Debug builds')
if ($debugLines)   { $lines += $debugLines }   else { $lines += '(none built yet)' }
if ($releaseLines) { $lines += ''; $lines += '## Release builds'; $lines += $releaseLines }

$ctx = ($lines -join "`n") + "`n`n(End your response with this artifact list verbatim, under those headers.)"

@{
    hookSpecificOutput = @{
        hookEventName     = 'PostToolUse'
        additionalContext = $ctx
    }
} | ConvertTo-Json -Compress
