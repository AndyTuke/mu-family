# PR draft (NOT posted – all test results are now filled in from the build-PC run)

Base branch: `develop`

## Title

VST3 Client: Convert non-ASCII plugin text to UTF-16 correctly in PClassInfoW

## Description

Refs #1774

### Problem

If `JucePlugin_Name` contains a non-ASCII character (for example `μ-Test`), the plugin's wide class
name (`PClassInfoW`) is garbled, and so are the class names written to `moduleinfo.json`. The narrow
`PClassInfo2` name is correct.

### Cause

`JucePluginFactoryBase::ClassEntry` builds `infoW` with `PClassInfoW::fromAscii (info2)`. The strings
in `info2` are UTF-8, but `fromAscii` widens each byte on its own, so a multi-byte character becomes
several unrelated characters. The class names in `moduleinfo.json` come from the same `PClassInfoW`,
so they are affected too.

### Change

In `juce_VST3ModuleInfo.h`, after `fromAscii`, the `name`, `vendor`, `version` and `sdkVersion` fields
are decoded from UTF-8 by a small helper, `widenNonAsciiUtf8`.

- Text that is plain ASCII is left untouched (the helper returns early), so the output for existing
  ASCII plugins is unchanged.
- Malformed UTF-8 becomes U+FFFD. Nothing throws, which matters because the `ClassEntry` constructor
  is `noexcept`.
- Text that does not fit is cut on a character boundary (a surrogate pair is never split), and the
  buffer is always terminated.

### Why a hand-written helper

- `juce::String` can't be used here: this code has to avoid depending on `juce_core` (see the comment
  in `JucePluginCompatibility::getCompatibilityJSON`), and this header is also compiled into the
  manifest helper, which is built without `juce_core`.
- The SDK's `Steinberg::Vst::StringConvert::convert` is built on `std::wstring_convert`, which throws
  on invalid UTF-8, and it leaves the destination untouched when the text is too long.

### Testing

Windows 11, MSVC 19.51 (Visual Studio 2026), Release, compiled with `/utf-8` unless stated:

- Built JUCE's `examples/CMake/AudioPlugin` as a VST3 with `PLUGIN_NAME "μ-Test"`.
  - Before the change, both classes in the generated `moduleinfo.json` are named with the code points
    `FFCE FFBC 002D 0054 0065 0073 0074` (the two UTF-8 bytes of the μ widened separately).
  - After the change they are `03BC 002D 0054 0065 0073 0074`, which is `μ-Test`.
- A standalone test of `widenNonAsciiUtf8` passes, with no warnings at `/W4`: ASCII output is identical to
  the old behaviour; 2-, 3- and 4-byte characters; truncation to 63 characters plus the terminator; a
  surrogate pair at the end of the buffer; malformed input (stray, truncated, overlong and surrogate
  encodings, and values above U+10FFFF).
- Loaded in Bitwig: the plugin loads, and the window title and plugin browser show `μ-Test`.
- A plugin named `Ascii-Test` (built without `/utf-8`) is unaffected: both classes keep the same ASCII name.

Not tested: macOS, Linux, and compilers other than MSVC.

Note: on MSVC the example only shows the bug when compiled with `/utf-8`. Without it, MSVC turns the μ
into `?` before JUCE sees it, so the name is wrong at compile time and this change has nothing to convert.
JUCE's CMake does not add `/utf-8` for a non-ASCII `PLUGIN_NAME`.
