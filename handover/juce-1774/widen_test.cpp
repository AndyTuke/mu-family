// Standalone test for widenNonAsciiUtf8 (juce_VST3ModuleInfo.h).
// run-widen-test.ps1 extracts the function from the patched header into widen_extracted.inc,
// so this tests the exact code that is in the patch.

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <iterator>
#include <string>

namespace Steinberg { using char16 = char16_t; }

#include "widen_extracted.inc"

static int failures = 0;

static void check (bool ok, const char* what)
{
    if (! ok)
    {
        std::printf ("FAIL: %s\n", what);
        ++failures;
    }
}

// The buffer must hold exactly the expected text, followed by zeros up to the end.
template <size_t N>
static bool bufferEquals (const char16_t (&buf)[N], const std::u16string& expected)
{
    for (size_t i = 0; i < N; ++i)
        if (buf[i] != (i < expected.size() ? expected[i] : char16_t (0)))
            return false;

    return true;
}

// What fromAscii leaves behind for an ASCII string: each byte widened, then zero padding.
template <size_t N>
static void oldWiden (char16_t (&buf)[N], const std::string& s)
{
    std::fill (std::begin (buf), std::end (buf), char16_t (0));

    for (size_t i = 0; i < s.size() && i + 1 < N; ++i)
        buf[i] = (char16_t) (unsigned char) s[i];
}

// Marks a buffer with garbage, as fromAscii leaves for non-ASCII text.
template <size_t N>
static void garble (char16_t (&buf)[N])
{
    std::fill (std::begin (buf), std::end (buf), char16_t (0x7777));
}

int main()
{
    // 1. ASCII text must come out identical to the old behaviour.
    {
        const std::string samples[]
        {
            "", "a", "Plain ASCII Name 1.0", "mu-Clid", "VST 3.8.0", std::string (63, 'x'), "Tab\tand~tilde"
        };

        for (const auto& s : samples)
        {
            char16_t before[64], after[64];
            oldWiden (before, s);
            std::copy (std::begin (before), std::end (before), std::begin (after));
            widenNonAsciiUtf8 (after, std::size (after), s.c_str());
            check (std::equal (std::begin (before), std::end (before), std::begin (after)), "ASCII output unchanged");
        }
    }

    // 2. Greek mu (the case from the bug report): U+03BC is 0xCE 0xBC in UTF-8.
    {
        char16_t buf[64];
        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "\xCE\xBC" "-Test");
        check (bufferEquals (buf, u"μ-Test"), "Greek mu name");
    }

    // 3. Two-, three- and four-byte characters together.
    {
        char16_t buf[64];
        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "\xC3\xA9" "\xE2\x82\xAC" "\xF0\x9D\x84\x9E"); // e-acute, euro, G clef
        check (bufferEquals (buf, u"é€\xD834\xDD1E"), "2, 3 and 4 byte characters");
    }

    // 4. Too long: 70 two-byte characters into 64 slots leaves 63 plus the terminator.
    {
        std::string longText;

        for (int i = 0; i < 70; ++i)
            longText += "\xC3\xA9";

        char16_t buf[64];
        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), longText.c_str());
        check (bufferEquals (buf, std::u16string (63, u'é')), "truncation to 63 characters plus terminator");
    }

    // 5. A surrogate pair is never split at the end of the buffer.
    {
        char16_t small[4];
        garble (small);
        widenNonAsciiUtf8 (small, std::size (small), "ab" "\xF0\x9D\x84\x9E");
        check (bufferEquals (small, u"ab"), "surrogate pair dropped whole when it does not fit");

        char16_t exact[5];
        garble (exact);
        widenNonAsciiUtf8 (exact, std::size (exact), "ab" "\xF0\x9D\x84\x9E");
        check (bufferEquals (exact, u"ab\xD834\xDD1E"), "surrogate pair kept when it fits exactly");
    }

    // 6. Malformed input becomes U+FFFD and never throws or overruns.
    {
        char16_t buf[64];

        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "\xCE");
        check (bufferEquals (buf, u"�"), "lead byte with no continuation");

        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "\x80");
        check (bufferEquals (buf, u"�"), "stray continuation byte");

        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "\xC0\x80");
        check (bufferEquals (buf, u"��"), "overlong encoding rejected");

        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "\xED\xA0\x80");
        check (bufferEquals (buf, u"���"), "UTF-8 encoded surrogate rejected");

        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "\xF4\x90\x80\x80");
        check (bufferEquals (buf, u"����"), "code point above U+10FFFF rejected");

        garble (buf);
        widenNonAsciiUtf8 (buf, std::size (buf), "ab" "\xE2\x82");
        check (bufferEquals (buf, u"ab��"), "sequence cut off by the end of the string");
    }

    // 7. A buffer of size 1 holds only the terminator.
    {
        char16_t tiny[1] { 0x7777 };
        widenNonAsciiUtf8 (tiny, 1, "\xCE\xBC");
        check (tiny[0] == 0, "size 1 buffer is terminated");
    }

    std::printf (failures == 0 ? "All widenNonAsciiUtf8 tests passed\n" : "%d test(s) FAILED\n", failures);
    return failures == 0 ? 0 : 1;
}
