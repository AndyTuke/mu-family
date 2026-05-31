# PostToolUse hook: detects cmake builds and flags the first build of the day
# for the mu-oversight audit (read by mu_oversight_inject.ps1).

$rawInput = [Console]::In.ReadToEnd()

try {
    $data = $rawInput | ConvertFrom-Json -ErrorAction Stop
    $cmd  = $data.tool_input.command

    if ($cmd -notmatch "cmake.*--build") { exit 0 }

    $flagFile = Join-Path $PSScriptRoot "..\oversight-pending"
    $today    = (Get-Date -Format "yyyy-MM-dd")

    if (Test-Path $flagFile) {
        if ((Get-Content $flagFile -Raw).Trim() -eq $today) { exit 0 }
    }

    $today | Set-Content -Path $flagFile -NoNewline
}
catch { }

exit 0
