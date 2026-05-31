# UserPromptSubmit hook: if the first cmake build of today was detected,
# prepends the mu-oversight audit request to the user's prompt so the
# current Claude session runs the audit (with local Discord MCP) before
# answering the user's actual message.

$rawInput = [Console]::In.ReadToEnd()

try {
    $data     = $rawInput | ConvertFrom-Json -ErrorAction Stop
    $flagFile = Join-Path $PSScriptRoot "..\oversight-pending"
    $today    = (Get-Date -Format "yyyy-MM-dd")

    if (-not (Test-Path $flagFile)) {
        Write-Output $rawInput
        exit 0
    }

    $pending = (Get-Content $flagFile -Raw -ErrorAction SilentlyContinue).Trim()
    if ($pending -ne $today) {
        Write-Output $rawInput
        exit 0
    }

    Remove-Item $flagFile -Force -ErrorAction SilentlyContinue

    $prefix = @"
[MU-OVERSIGHT TRIGGER: This is the first cmake build of $today. Before responding to my message below, perform the full mu-oversight audit:
1. Check mu-core boundary violations (grep for product includes/namespaces in mu-core/)
2. Check code placement (classes duplicated across products that should be in mu-core)
3. Check UX consistency (hardcoded colours/sizes, non-shared components) vs docs/design-ui-family.md
4. Check design-doc alignment (docs/design-plugin-family.md, docs/design-ui-family.md, docs/design-fx.md vs implementation)
5. Architectural spot-checks (ModulationMatrix as single reader, FXSlotBase inheritance, MuClidLookAndFeel in mu-core)
Post findings to Discord, commit a report to reports/oversight-$today.md, and add new issues to backlog.md.
Then respond to my actual message.]

"@

    $data.prompt = $prefix + $data.prompt
    $data | ConvertTo-Json -Compress -Depth 10
}
catch {
    Write-Output $rawInput
}

exit 0
