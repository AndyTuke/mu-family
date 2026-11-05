# PR draft (NOT posted – all test results are filled in from the build-PC run)

Base branch: `develop`

## Title

VST3 Client: Convert non-ASCII plugin text to UTF-16 correctly in PClassInfoW

## Description

Refs #1774

If `JucePlugin_Name` contains a non-ASCII character (mine has a Greek mu, `μ-Test`), the wide class name in `PClassInfoW` comes out garbled, and so do the class names in the generated `moduleinfo.json`. The narrow `PClassInfo2` name is fine.

The cause is in `juce_VST3ModuleInfo.h`. `ClassEntry` builds `infoW` with `PClassInfoW::fromAscii (info2)`, but the strings in `info2` are UTF-8 and `fromAscii` widens one byte at a time, so a two-byte character such as U+03BC turns into two unrelated characters. `moduleinfo.json` takes its class names from the same `PClassInfoW`, so it ends up wrong as well.

This adds a small helper, `widenNonAsciiUtf8`, which runs over the `name`, `vendor`, `version` and `sdkVersion` fields after `fromAscii`:

- Plain ASCII text returns early, so existing plugins get exactly the same output as before.
- Bad UTF-8 becomes U+FFFD rather than throwing, as the constructor is `noexcept`.
- Text that is too long is cut at a character boundary, never in the middle of a surrogate pair, and the buffer is always terminated.

I wrote the decoder by hand because nothing else seemed to fit. `juce::String` is out, as this code is meant to avoid `juce_core` (there's a comment saying so in `getCompatibilityJSON`) and the header is also built into the manifest helper, which doesn't use `juce_core`. The SDK's `StringConvert::convert` uses `std::wstring_convert`, which throws on invalid UTF-8, and it leaves the buffer untouched if the text doesn't fit. I'm happy to change the approach if you'd prefer something different.

Testing was on Windows 11 with MSVC 19.51 (Visual Studio 2026), Release, compiled with `/utf-8` unless I say otherwise:

- I built the `examples/CMake/AudioPlugin` example as a VST3 with `PLUGIN_NAME "μ-Test"`. Before the change, both classes in `moduleinfo.json` have the code points `FFCE FFBC 002D 0054 0065 0073 0074`, which is the two UTF-8 bytes of the mu widened separately. After it they are `03BC 002D 0054 0065 0073 0074`, which is `μ-Test`.
- A standalone test of the helper passes with no warnings at `/W4`. It covers ASCII output matching the old behaviour, 2-, 3- and 4-byte characters, truncation to 63 characters plus the terminator, a surrogate pair at the end of the buffer, and malformed input (stray, truncated, overlong and surrogate encodings, and values above U+10FFFF).
- The plugin loads in Bitwig, and the window title and plugin browser show `μ-Test`.
- A plugin named `Ascii-Test`, built without `/utf-8`, is unaffected: both classes keep the same ASCII name.

I haven't tried macOS, Linux or any compiler other than MSVC.

One thing I noticed along the way: on MSVC the bug only shows if the project is compiled with `/utf-8`. Without it, MSVC turns the mu into a `?` before JUCE sees the string, so there is nothing for this change to fix. JUCE's CMake doesn't add `/utf-8` for a non-ASCII `PLUGIN_NAME`, which may be worth a separate look.
