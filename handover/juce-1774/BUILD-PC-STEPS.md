# JUCE #1774 – verify the patch on the build PC

Nothing here touches mu-family or your normal `JUCE_PATH`. Use a separate folder **not on C:**
(examples use `D:\juce-1774`). All commands: Visual Studio **x64 Native Tools / Developer PowerShell**.

The scripts have NOT been run yet (they were written on the review-only laptop, which has no compiler).
If a script fails for a reason that is not the patch, fix the script first and note it.

## 1. Get JUCE and apply the patch

```powershell
mkdir D:\juce-1774 ; cd D:\juce-1774
git clone --branch develop https://github.com/juce-framework/JUCE.git JUCE
cd JUCE
git config user.name  "Andy Tuke"
git config user.email "30471813+AndyTuke@users.noreply.github.com"
git switch -c vst3-wide-class-name-utf8
git am --keep-cr <mu-family>\handover\juce-1774\0001-VST3-Client-Convert-non-ASCII-plugin-text-to-UTF-16.patch
```

`--keep-cr` is required: the header uses CRLF line endings and a plain `git am` fails on it.
Expect one line: `warning: quoted CRLF detected` and then `Applying: VST3 Client: ...`.
Check: `git log -1 --format="%an <%ae>"` must show the noreply address, not a Softcat one.

## 2. Standalone test of the helper (about a minute)

```powershell
cd <mu-family>\handover\juce-1774
.\run-widen-test.ps1 -JuceRoot D:\juce-1774\JUCE -OutDir D:\juce-1774\widen-test
```

Expected: `All widenNonAsciiUtf8 tests passed`. Any `FAIL:` line is a real finding – send it back.
Compiler warnings at `/W4` should be looked at too (JUCE builds with strict warnings).

## 3. Real plugin, before and after

First the UNPATCHED code (this should show the bug):

```powershell
cd D:\juce-1774\JUCE ; git switch develop
cd <mu-family>\handover\juce-1774
.\check-real-build.ps1 -JuceRoot D:\juce-1774\JUCE -WorkDir D:\juce-1774\work -Label unpatched
```
Expected: `FAIL` lines with the wrong code points (the first character is not `03BC`).

Then the PATCHED code:

```powershell
cd D:\juce-1774\JUCE ; git switch vst3-wide-class-name-utf8
cd <mu-family>\handover\juce-1774
.\check-real-build.ps1 -JuceRoot D:\juce-1774\JUCE -WorkDir D:\juce-1774\work -Label patched
```
Expected: `PASS` for every class, code points `03BC 002D 0054 0065 0073 0074`.

If "unpatched" PASSES, the bug does not reproduce this way (for example the compiler did not pass the
name as UTF-8) – stop and report, do not continue to a PR.

## 4. Optional but worthwhile

- Load the patched `.vst3` in a host that shows the plugin name (Reaper, Bitwig, …) and confirm the
  name is right and the plugin loads.
- Build one of your own plugins (`μ-Clid`) against `D:\juce-1774\JUCE` if the pin check allows it
  (mu-family pins JUCE 9.0.4, so this may be refused – that is fine, steps 2 and 3 are enough).
- An ASCII-named plugin (for example the unmodified example) must be completely unaffected.

## 5. Report back

Write the results into `handover/build-to-laptop.md` under `## Results` as `### Re L-012` (format in
`handover/README.md`): for steps 2, 3 (both runs) and 4, whether it passed, plus any warnings or odd output. The PR draft has blanks for exactly these results.

## Results seen on the build PC (2026-11-05)

- Step 2 passed, no warnings (needed `/utf-8` on its `cl` line).
- Step 3 needs `-Utf8` (compile with `/utf-8`): unpatched FAIL (`FFCE FFBC ...`), patched PASS (`03BC ...`). Without `/utf-8` both give `?-Test`.
- ASCII check: `.\check-real-build.ps1 -Ascii ...` on the patched branch passes (`Ascii-Test`).
- Step 4, Bitwig: title bar and plug-in browser show `μ-Test` correctly.
- Toolchain: MSVC `cl` 19.51.36260 x64 (Visual Studio 2026, toolset 14.51.36231), CMake 4.3.1, Windows 11 Pro 10.0.26300.
