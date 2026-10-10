# check-comment-refs.ps1 — guard: source comments must not reference backlog issue numbers.
#
# CLAUDE.md "Code style" forbids `// #N`, `// fix for #N`, `// added in #N` and any other
# backlog reference in source: the context belongs in the commit message, and a stale number
# is worse than none. Nothing enforced it, so the references crept back.
#
# How it works: a small lexer (embedded C#, so it is fast) walks each file and extracts ONLY the
# comment text. It understands string / char / raw-string literals, #include <...>, #error text,
# CMake bracket arguments, PowerShell here-strings and Python triple quotes, so a string holding a
# sharp or `#include <a//b>` is never mistaken for a comment, and #include / #define / #pragma
# lines are code, not comments. For '#' comment languages (CMake, PowerShell, YAML, Python) the
# leading '#' stays in the text, so a number directly after the marker matches. Each comment line
# is matched for:
#   1. `#N`            N = 1-5 digits, starting a token (line start, whitespace, ( [ { , ; : # or a
#                      dash) and not followed by a word character. 6-digit hex colours and external
#                      ids never match; #378ADD, 1/#2, a#3, C#4 and &#169; fail the boundaries.
#   2. `issue|backlog|ticket|bug [#] N`   (a bare single digit and 19xx/20xx years are ignored)
#   3. `GH-N` `GH#N` `BL-N` `BUG-N` `ISSUE-N`   (case-insensitive)
#   4. `closes|fixes|resolves [#] N`
# and then the false-positive filters (all listed by -ShowSuppressed so they can be audited):
#   - `ref-ok` (whole word, case-sensitive) anywhere in the same comment segment
#   - token containing "://" (URL fragment such as page.html#12)
#   - the word before the number is store|order|invoice|rfc|decision|customer
#   - a 3-4 digit number in a comment that also says colour/hex/rgb/0x
#
# Scanned: .h .hpp .cpp .cc .c .mm .cmake CMakeLists.txt .ps1 .yml .yaml .py. Skipped (matched on
# the path relative to the scan root, at any depth): build, build-*, cmake-build*, ThirdParty,
# JUCE, _deps, node_modules, .git, _out, archive, fixtures. So ANY directory named build is skipped;
# fixtures holds the checker's own deliberate violations (see test-check-comment-refs.ps1).
#
# Known limits:
#   - `ref-ok` only covers its own comment segment: in `/* #15 */ // ref-ok` the block comment is
#     a separate segment and still fails. One `ref-ok` hides every reference in its segment.
#   - Bare-number prose ("see 12", "fixed in b972") is not detected; ordinals such as
#     "rule #1" or "Pad #3" are flagged (use ref-ok).
#   - `#if 0` blocks are scanned as live code; YAML block scalars are scanned as normal lines.
#
#   pwsh tests/scripts/check-comment-refs.ps1                  # check the whole repo
#   pwsh tests/scripts/check-comment-refs.ps1 -Path mu-core    # check one tree / one file
#   pwsh tests/scripts/check-comment-refs.ps1 -Quiet           # summary line only
#   pwsh tests/scripts/check-comment-refs.ps1 -ShowSuppressed  # also list filtered-out matches
#
# Exit code 0 = clean, 1 = one or more violations, 2 = setup error (path missing).

param(
    [string]$Path,
    [switch]$Quiet,
    [switch]$ShowSuppressed
)

$ErrorActionPreference = 'Stop'

# Violation lines carry an em-dash. Force UTF-8 out so it survives redirection.
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding($false) } catch { }

# Repo root: this script lives in tests/scripts/.
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
# Outside tests/scripts (e.g. run from a scratch dir) the repo root is unknown: fall back to the working directory.
if ((Split-Path -Leaf $PSScriptRoot) -ne 'scripts') { $repoRoot = (Get-Location).Path }
if (-not $Path) { $Path = $repoRoot }

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Host "check-comment-refs: SETUP ERROR - path not found: $Path"
    exit 2
}
$Path = (Resolve-Path -LiteralPath $Path).Path

function Get-RepoRelativePath([string]$full) {
    if ($full.StartsWith($repoRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        $full = $full.Substring($repoRoot.Length).TrimStart('\', '/')
    }
    return $full.Replace('\', '/')
}

# Comment extractor. Mode 0 = C-family (// and /* */), 1 = CMake (# and #[[ ]]), 2 = PowerShell (# and <# #>).
# Returns "line<TAB>text" strings, one per comment line (block comments are split per line).
Add-Type -Language CSharp -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;

public static class CommentLexer
{
    public static List<string> Extract(string s, int mode)
    {
        var res = new List<string>();
        int n = s.Length, i = 0, line = 1;
        var sb = new StringBuilder();
        int startLine = 1;
        Action<int> flush = (ln) => { if (sb.Length > 0) { res.Add(ln + "\t" + sb.ToString()); sb.Clear(); } };

        while (i < n)
        {
            char c = s[i];
            if (c == '\n') { line++; i++; continue; }

            if (mode == 0)
            {
                // #include <...> : a "//" inside the angle brackets is not a comment.
                if (c == '#')
                {
                    int ls = i; while (ls > 0 && s[ls - 1] != '\n') ls--;
                    string head = s.Substring(ls, i - ls);
                    if (head.Trim().Length == 0)
                    {
                        int j = i + 1; while (j < n && (s[j] == ' ' || s[j] == '\t')) j++;
                        if (string.CompareOrdinal(s, j, "include", 0, 7) == 0 || string.CompareOrdinal(s, j, "import", 0, 6) == 0)
                        {
                            while (j < n && s[j] != '<' && s[j] != '"' && s[j] != '\n') j++;
                            if (j < n && s[j] == '<') { while (j < n && s[j] != '>' && s[j] != '\n') j++; i = j; }
                            else i = j;
                            if (i < n && s[i] != '\n') i++;
                            continue;
                        }
                        if (string.CompareOrdinal(s, j, "error", 0, 5) == 0 || string.CompareOrdinal(s, j, "warning", 0, 7) == 0)
                        {
                            while (j < n && s[j] != '\n' && !(s[j] == '/' && j + 1 < n && (s[j + 1] == '/' || s[j + 1] == '*'))) j++;
                            i = j; continue;
                        }
                    }
                    i++; continue;
                }
                if (c == '/' && i + 1 < n && s[i + 1] == '/')
                {
                    // Line comment; a trailing backslash continues it onto the next line.
                    i += 2; startLine = line;
                    while (i < n)
                    {
                        if (s[i] == '\n')
                        {
                            bool cont = sb.Length > 0 && sb[sb.Length - 1] == '\\' || (sb.Length > 1 && sb[sb.Length - 1] == '\r' && sb[sb.Length - 2] == '\\');
                            flush(startLine);
                            if (!cont) break;
                            line++; startLine = line; i++; continue;
                        }
                        sb.Append(s[i]); i++;
                    }
                    continue;
                }
                if (c == '/' && i + 1 < n && s[i + 1] == '*')
                {
                    i += 2; startLine = line;
                    while (i < n && !(s[i] == '*' && i + 1 < n && s[i + 1] == '/'))
                    {
                        if (s[i] == '\n') { flush(startLine); line++; startLine = line; }
                        else sb.Append(s[i]);
                        i++;
                    }
                    flush(startLine);
                    i = Math.Min(n, i + 2);
                    continue;
                }
                if (c == '"')
                {
                    // Raw string R"delim( ... )delim" or ordinary string with backslash escapes.
                    if (i > 0 && s[i - 1] == 'R')
                    {
                        int p = s.IndexOf('(', i);
                        if (p > 0 && p - i - 1 <= 16)
                        {
                            string delim = ")" + s.Substring(i + 1, p - i - 1) + "\"";
                            int e = s.IndexOf(delim, p, StringComparison.Ordinal);
                            int stop = e < 0 ? n : e + delim.Length;
                            for (int k = i; k < stop; k++) if (s[k] == '\n') line++;
                            i = stop; continue;
                        }
                    }
                    i++;
                    while (i < n && s[i] != '"' && s[i] != '\n') { if (s[i] == '\\' && i + 1 < n) { if (s[i + 1] == '\n') line++; i++; } i++; }
                    if (i < n && s[i] == '"') i++;
                    continue;
                }
                if (c == '\'')
                {
                    // Digit separator (1'000) is not a char literal.
                    if (i > 0 && Uri.IsHexDigit(s[i - 1]) && i + 1 < n && Uri.IsHexDigit(s[i + 1])) { bool num = false; int k = i - 1; while (k >= 0 && (char.IsLetterOrDigit(s[k]) || s[k]=='\'')) k--; if (k + 1 < i && char.IsDigit(s[k + 1])) num = true; if (num) { i++; continue; } }
                    i++;
                    while (i < n && s[i] != '\'' && s[i] != '\n') { if (s[i] == '\\' && i + 1 < n) i++; i++; }
                    if (i < n && s[i] == '\'') i++;
                    continue;
                }
                i++; continue;
            }

            // Modes 1 (CMake), 2 (PowerShell), 3 (YAML), 4 (Python): '#' comments.
            if (mode == 2 && c == '<' && i + 1 < n && s[i + 1] == '#')
            {
                i += 2; startLine = line;
                while (i < n && !(s[i] == '#' && i + 1 < n && s[i + 1] == '>'))
                {
                    if (s[i] == '\n') { flush(startLine); line++; startLine = line; }
                    else sb.Append(s[i]);
                    i++;
                }
                flush(startLine);
                i = Math.Min(n, i + 2);
                continue;
            }
            if (mode == 2 && c == '@' && i + 1 < n && (s[i + 1] == '\'' || s[i + 1] == '"'))
            {
                // Here-string: runs to a line that begins with '@ or "@ .
                char q = s[i + 1];
                int e = s.IndexOf("\n" + q + "@", i, StringComparison.Ordinal);
                int stop = e < 0 ? n : e + 3;
                for (int k = i; k < stop; k++) if (s[k] == '\n') line++;
                i = stop; continue;
            }
            if (c == '#')
            {
                bool atTokenStart = i == 0 || " \t\r\n;(){}|,".IndexOf(s[i - 1]) >= 0;
                if (atTokenStart)
                {
                    if (mode == 1 && i + 1 < n && s[i + 1] == '[')
                    {
                        // CMake bracket comment #[==[ ... ]==]
                        int j = i + 2; int eq = 0; while (j < n && s[j] == '=') { eq++; j++; }
                        if (j < n && s[j] == '[')
                        {
                            string close = "]" + new string('=', eq) + "]";
                            int e = s.IndexOf(close, j, StringComparison.Ordinal);
                            int stop = e < 0 ? n : e + close.Length;
                            startLine = line; i = j + 1;
                            while (i < stop - close.Length || (e < 0 && i < n))
                            {
                                if (s[i] == '\n') { flush(startLine); line++; startLine = line; } else sb.Append(s[i]);
                                i++;
                            }
                            flush(startLine);
                            i = stop; continue;
                        }
                    }
                    startLine = line;  // keep the leading # in the text so "#N" directly after the marker matches
                    while (i < n && s[i] != '\n') { sb.Append(s[i]); i++; }
                    flush(startLine);
                    continue;
                }
                i++; continue;
            }
            // CMake bracket argument [==[ ... ]==] : literal text, not a comment.
            if (mode == 1 && c == '[')
            {
                int j = i + 1; int eq = 0; while (j < n && s[j] == '=') { eq++; j++; }
                if (j < n && s[j] == '[')
                {
                    string close = "]" + new string('=', eq) + "]";
                    int e = s.IndexOf(close, j, StringComparison.Ordinal);
                    int stop = e < 0 ? n : e + close.Length;
                    for (int k = i; k < stop; k++) if (s[k] == '\n') line++;
                    i = stop; continue;
                }
            }
            // Python triple-quoted strings.
            if (mode == 4 && (c == '"' || c == '\'') && i + 2 < n && s[i + 1] == c && s[i + 2] == c)
            {
                string close = new string(c, 3);
                int e = s.IndexOf(close, i + 3, StringComparison.Ordinal);
                int stop = e < 0 ? n : e + 3;
                for (int k = i; k < stop; k++) if (s[k] == '\n') line++;
                i = stop; continue;
            }
            bool quoteOk = c == '"' || (c == '\'' && mode >= 2);
            if (quoteOk && mode == 3)
            {
                // YAML: a quote only opens a string after ':' '-' '[' '{' ',' or at line start (else it is an apostrophe).
                int k = i - 1; while (k >= 0 && (s[k] == ' ' || s[k] == '\t')) k--;
                quoteOk = k < 0 || s[k] == '\n' || ":-[{,".IndexOf(s[k]) >= 0;
            }
            if (quoteOk)
            {
                // PowerShell escapes with a backtick, CMake/YAML/Python with a backslash; a single-quoted
                // PowerShell / YAML string has no escapes (doubled quotes just close and reopen).
                char q = c;
                bool noEsc = c == '\'' && (mode == 2 || mode == 3);
                char esc = mode == 2 ? '`' : '\\';
                i++;
                while (i < n && s[i] != q)
                {
                    if (!noEsc && s[i] == esc && i + 1 < n) { if (s[i + 1] == '\n') line++; i++; }
                    else if (s[i] == '\n') { line++; if (mode >= 3) break; }
                    i++;
                }
                i++; continue;
            }            i++;
        }
        flush(startLine);
        return res;
    }
}
'@

# Directories never scanned (build output, vendored / third-party code, VCS). Matched against the
# path RELATIVE to the scan root, so a checkout that itself sits under a folder called build/ or
# archive/ is still scanned.
$skipDir = [regex]'(?i)(^|/)(build|build-[^/]*|cmake-build[^/]*|ThirdParty|JUCE|_deps|node_modules|\.git|_out|archive|fixtures)/'
$exts    = @('.h', '.hpp', '.cpp', '.cc', '.c', '.mm', '.cmake', '.ps1', '.yml', '.yaml', '.py')

# Path relative to the scan root, forward slashes (used for the skip test).
$scanRoot = if (Test-Path -LiteralPath $Path -PathType Container) { $Path } else { Split-Path -Parent $Path }
function Get-ScanRelative([string]$full) {
    return $full.Substring($scanRoot.Length).TrimStart('\', '/').Replace('\', '/')
}

if (Test-Path -LiteralPath $Path -PathType Leaf) {
    $files = @(Get-Item -LiteralPath $Path)
} else {
    $files = @(Get-ChildItem -LiteralPath $Path -Recurse -File -Force |
        Where-Object { ($exts -contains $_.Extension.ToLowerInvariant() -or $_.Name -ieq 'CMakeLists.txt') -and -not $skipDir.IsMatch((Get-ScanRelative $_.FullName)) } |
        Sort-Object FullName)
}

# Fail closed: an empty scan means the filter or path is wrong, not that the tree is clean.
if ($files.Count -eq 0) {
    Write-Host "check-comment-refs: SETUP ERROR - no source files found under $Path"
    exit 2
}
# --- matchers -------------------------------------------------------------------------------
# "#N" must start a token (line start, whitespace, ( [ { , ; : or another #) so 1/#2, a#3, &#169; and C#4 are out.
$hashRef   = [regex]'(?<=^|[\s(\[{,;:#\u2013\u2014])#(\d{1,5})(?!\w)'
$wordRef   = [regex]'(?i)\b(?:issue|issues|backlog|ticket|bug)s?\s*(?:item\s*)?(?:no\.?\s*)?#?\s*(\d{1,5})(?![\w.])'
$idRef     = [regex]'(?i)\b(?:GH|BL|BUG|ISSUE)[-#]\s?(\d{1,5})(?!\w)'
$verbRef   = [regex]'(?i)\b(?:closes|fixes|resolves)\s*:?\s*#?(\d{1,5})(?![\w.])'
# Nouns that make "#N" an external id / design-doc number rather than a backlog item. Kept tiny on
# purpose: words like layer/step/note/track are realistic ways to write a backlog reference.
$notBacklog = [regex]'(?i)\b(?:store|order|invoice|rfc|decision|customer)\s*$'
$colourCtx  = [regex]'(?i)colou?r|\bhex\b|\brgb|\bargb|0x'
# Per-line opt-out for a deliberate, legitimate "#N" in a comment.
$optOut     = [regex]'\bref-ok\b'
$total = 0; $fails = 0; $suppressed = 0

foreach ($file in $files) {
    $total++
    $name = $file.Name.ToLowerInvariant()
    $ext  = $file.Extension.ToLowerInvariant()
    $mode = if ($ext -eq '.ps1') { 2 } elseif ($ext -eq '.cmake' -or $name -eq 'cmakelists.txt') { 1 } elseif ($ext -eq '.yml' -or $ext -eq '.yaml') { 3 } elseif ($ext -eq '.py') { 4 } else { 0 }
    $text = [System.IO.File]::ReadAllText($file.FullName)
    $rel  = Get-RepoRelativePath $file.FullName

    foreach ($entry in [CommentLexer]::Extract($text, $mode)) {
        $tab     = $entry.IndexOf("`t")
        $lineNo  = [int]$entry.Substring(0, $tab)
        $comment = $entry.Substring($tab + 1)
        $snippet = $comment.Trim()
        if ($snippet.Length -gt 90) { $snippet = $snippet.Substring(0, 90) }

        # One report per comment line, even if several numbers match.
        $hits = @()
        foreach ($rx in @($hashRef, $wordRef, $idRef, $verbRef)) {
            foreach ($m in $rx.Matches($comment)) { $hits += , $m }
        }
        $reported = $false
        foreach ($m in ($hits | Sort-Object { $_.Index })) {
            $why = $null
            # URL fragment: the whitespace-delimited token around the match contains "://".
            $ts = $comment.LastIndexOfAny(@(' ', "`t"), $m.Index) + 1
            $te = $comment.IndexOfAny(@(' ', "`t"), $m.Index); if ($te -lt 0) { $te = $comment.Length }
            if ($optOut.IsMatch($comment)) { $why = 'ref-ok marker' }
            elseif ($comment.Substring($ts, $te - $ts).Contains('://')) { $why = 'URL fragment' }
            elseif ($notBacklog.IsMatch($comment.Substring(0, $m.Index))) { $why = 'preceded by a non-backlog noun' }
            elseif ($m.Groups[1].Value.Length -in 3,4 -and $m.Value.StartsWith('#') -and $colourCtx.IsMatch($comment)) { $why = 'short hex colour' }
            elseif ($m.Value -notmatch '#' -and $m.Groups[1].Value -match '^(19|20)\d\d$') { $why = 'year' }
            elseif ($m.Value -notmatch '[#-]' -and $m.Groups[1].Value.Length -lt 2) { $why = 'single-digit prose number' }

            if ($why) {
                $suppressed++
                if ($ShowSuppressed -and -not $Quiet) { Write-Host "  [skip] ${rel}:${lineNo} — $why ($($m.Value.Trim())): $snippet" }
                continue
            }
            if (-not $reported) {
                if (-not $Quiet) { Write-Host "  [FAIL] ${rel}:${lineNo} — comment references a backlog issue ($($m.Value.Trim())): $snippet" }
                $fails++
                $reported = $true
            }
        }
    }
}

$status = if ($fails -eq 0) { 'PASS' } else { 'FAIL' }
Write-Host "check-comment-refs: $status ($total source files, $fails violation(s), $suppressed suppressed)"
exit $(if ($fails -eq 0) { 0 } else { 1 })
