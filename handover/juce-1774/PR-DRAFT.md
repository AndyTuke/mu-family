# PR draft (NOT posted – blanks marked TODO are filled in after the build-PC run)

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

After `fromAscii`, the `name`, `vendor`, `version` and `sdkVersion` fields are decoded from UTF-8 by a
small helper, `widenNonAsciiUtf8`.

- Text that is plain ASCII is left untouched (the helper returns early), so the output for existing
  ASCII plugins is unchanged.
- Malformed UTF-8 becomes U+FFFD. Nothing throws, which matters because the `ClassEntry` constructor
  is `noexcept`.
- Text that does not fit is cut on a character boundary (a surrogate pair is never split), and the
  buffer is always terminated.

### Why a hand-written helper

- `juce::String` can't be used here: this code has to avoid depending on `juce_core` (see the comment
  in `JucePluginCompatibility::getCompatibilityJSON`), and this header is also compiled into the
  manifest helper, where `juce_core` is stubbed.
- The SDK's `Steinberg::Vst::StringConvert::convert` is built on `std::wstring_convert`, which throws
  on invalid UTF-8, and it leaves the destination untouched when the text is too long.

### Testing

- TODO: Windows / MSVC <version>: built JUCE's `examples/CMake/AudioPlugin` as a VST3 with
  `PLUGIN_NAME "μ-Test"`. Before the change the class names in `moduleinfo.json` are garbled
  (TODO: code points); after it they are `μ-Test`.
- TODO: standalone test of the helper: ASCII output identical to the old behaviour; 2-, 3- and 4-byte
  characters; truncation; surrogate pair at the end of the buffer; malformed input.
- TODO: loaded in <host> and the name displays correctly.
- Not tested: macOS and Linux.
