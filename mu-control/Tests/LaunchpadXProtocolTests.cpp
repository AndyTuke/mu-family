// LaunchpadXProtocol: control numbering, input decoding and the exact SysEx bytes, including the messages
// recorded on the real pad (design-launchpad 3.5). The RGB lighting layout is from the Programmer's reference
// and not yet seen on hardware.

#include "Devices/LaunchpadX/LaunchpadXPalette.h"
#include "Devices/LaunchpadX/LaunchpadXProtocol.h"
#include <juce_core/juce_core.h>
#include <set>

namespace
{
using namespace mu_control;
using namespace mu_control::launchpadx;
using Bytes = std::vector<uint8_t>;

std::vector<SurfaceControlId> allControls()
{
    std::vector<SurfaceControlId> all;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) all.push_back({ ControlKind::Pad, x, y });
    for (int i = 0; i < 8; ++i) all.push_back({ ControlKind::Top, i, 0 });
    for (int i = 0; i < 8; ++i) all.push_back({ ControlKind::Side, 0, i });
    return all;
}
}

class LaunchpadXProtocolTests : public juce::UnitTest
{
public:
    LaunchpadXProtocolTests() : juce::UnitTest("Launchpad X protocol", "MuControl") {}

    void runTest() override
    {
        beginTest("mode and setup messages are the recorded bytes");
        expect(programmerMode(true)  == Bytes { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7 });
        expect(programmerMode(false) == Bytes { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x00, 0xF7 });
        expect(aftertouchOff()       == Bytes { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0B, 0x02, 0x01, 0xF7 });
        expect(readLayout()          == Bytes { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x00, 0xF7 });
        expect(deviceInquiry()       == Bytes { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 });

        beginTest("the recorded device inquiry reply is recognised, others are not");
        {
            const Bytes reply { 0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x03, 0x01, 0x00, 0x00, 0x00, 0x04, 0x02, 0x02, 0xF7 };
            expect(isInquiryReply(reply.data(), reply.size()));
            Bytes otherMaker = reply; otherMaker[7] = 0x30;
            expect(! isInquiryReply(otherMaker.data(), otherMaker.size()));
            Bytes otherFamily = reply; otherFamily[8] = 0x04;
            expect(! isInquiryReply(otherFamily.data(), otherFamily.size()));
            const auto layout = readLayout();
            expect(! isInquiryReply(layout.data(), layout.size()));
            expect(! isInquiryReply(reply.data(), 5), "a short message");
            expect(! isInquiryReply(nullptr, 0));
        }

        beginTest("numbering: pad = 10 * row + column with the bottom row first, buttons on the CC numbers");
        expectEquals(*indexOf({ ControlKind::Pad, 0, 7 }), 11);   // bottom-left
        expectEquals(*indexOf({ ControlKind::Pad, 7, 7 }), 18);
        expectEquals(*indexOf({ ControlKind::Pad, 0, 0 }), 81);   // top-left
        expectEquals(*indexOf({ ControlKind::Pad, 7, 0 }), 88);
        expectEquals(*indexOf({ ControlKind::Top, 0, 0 }), 91);
        expectEquals(*indexOf({ ControlKind::Top, 7, 0 }), 98);
        expectEquals(*indexOf({ ControlKind::Side, 0, 7 }), 19);  // bottom button
        expectEquals(*indexOf({ ControlKind::Side, 0, 0 }), 89);  // top button
        expectEquals(*indexOf({ ControlKind::Special, 0, 0 }), 99);   // the logo
        expect(! indexOf({ ControlKind::Special, 1, 0 }) && ! indexOf({ ControlKind::Special, 0, 1 }), "there is one logo");
        expect(! indexOf({ ControlKind::Pad, 8, 0 }) && ! indexOf({ ControlKind::Pad, 0, -1 }));
        expect(! indexOf({ ControlKind::Top, 8, 0 }) && ! indexOf({ ControlKind::Side, 0, 8 }));

        beginTest("every pad and button round trips through its index and through its input bytes");
        {
            std::set<int> pads, ccs;
            for (const auto& id : allControls())
            {
                const auto index = indexOf(id);
                expect(index.has_value());
                if (! index) continue;
                const bool isCc = id.kind != ControlKind::Pad;
                (isCc ? ccs : pads).insert(*index);
                const auto back = controlOf(*index, isCc);
                expect(back.has_value() && *back == id);

                for (const bool down : { true, false })
                {
                    const auto bytes = encodeInput(id, down);
                    const auto events = decodeInput(bytes.data(), bytes.size());
                    expectEquals((int) events.size(), 1);
                    if (events.size() == 1)
                        expect(events[0].id == id && events[0].type == (down ? SurfaceEventType::Press : SurfaceEventType::Release));
                }
            }
            expectEquals((int) pads.size(), 64, "64 distinct pad notes");
            expectEquals((int) ccs.size(), 16, "16 distinct button CCs");
        }

        beginTest("input bytes: exact messages, velocity 0 and note-off release, CC 127 and 0");
        {
            const Bytes noteOn { 0x90, 11, 100 }, noteVel0 { 0x90, 11, 0 }, noteOff { 0x80, 11, 64 };
            expect(encodeInput({ ControlKind::Pad, 0, 7 }, true) == noteOn);
            expect(encodeInput({ ControlKind::Pad, 0, 7 }, false) == noteVel0);
            expect(encodeInput({ ControlKind::Top, 2, 0 }, true) == Bytes { 0xB0, 93, 127 });
            expect(encodeInput({ ControlKind::Side, 0, 7 }, false) == Bytes { 0xB0, 19, 0 });
            expect(encodeInput({ ControlKind::Special, 0, 0 }, true) == Bytes { 0xB0, 99, 127 });
            expect(encodeInput({ ControlKind::Special, 0, 0 }, false) == Bytes { 0xB0, 99, 0 });
            expect(decodeInput(noteVel0.data(), noteVel0.size())[0].type == SurfaceEventType::Release);
            expect(decodeInput(noteOff.data(), noteOff.size())[0].type == SurfaceEventType::Release);
        }

        beginTest("the logo reports its press and release like a button");
        {
            const Bytes down { 0xB0, 99, 127 }, up { 0xB0, 99, 0 }, notePad { 0x90, 99, 100 };
            const auto pressed  = decodeInput(down.data(), down.size());
            const auto released = decodeInput(up.data(), up.size());
            expectEquals((int) pressed.size(), 1);
            expectEquals((int) released.size(), 1);
            if (pressed.size() == 1 && released.size() == 1)
            {
                expect(pressed[0].id == SurfaceControlId { ControlKind::Special, 0, 0 } && pressed[0].type == SurfaceEventType::Press);
                expect(released[0].id == pressed[0].id && released[0].type == SurfaceEventType::Release);
            }
            expect(decodeInput(notePad.data(), notePad.size()).empty(), "99 as a note is not on the grid");
        }

        beginTest("input that is not a pad or button is ignored: aftertouch, SysEx, strays, truncated");
        {
            const Bytes polyAt { 0xA0, 11, 50 }, chanAt { 0xD0, 50 }, unknownNote { 0x90, 9, 100 },
                        outOfGrid { 0x90, 19, 100 }, unknownCc { 0xB0, 7, 127 }, sysex { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7 },
                        stray { 0x40, 0xF8 }, truncated { 0x90, 11 };
            for (const auto* m : { &polyAt, &chanAt, &unknownNote, &outOfGrid, &unknownCc, &sysex, &stray, &truncated })
                expect(decodeInput(m->data(), m->size()).empty());
            expect(decodeInput(nullptr, 0).empty());
        }

        beginTest("several messages in one buffer decode in order, around a SysEx");
        {
            Bytes buf { 0x90, 11, 100 };
            const auto sx = programmerMode(true);
            buf.insert(buf.end(), sx.begin(), sx.end());
            buf.insert(buf.end(), { 0xB0, 91, 127, 0x90, 88, 0 });
            const auto ev = decodeInput(buf.data(), buf.size());
            expectEquals((int) ev.size(), 3);
            if (ev.size() == 3)
            {
                expect(ev[0].id == SurfaceControlId { ControlKind::Pad, 0, 7 } && ev[0].type == SurfaceEventType::Press);
                expect(ev[1].id == SurfaceControlId { ControlKind::Top, 0, 0 });
                expect(ev[2].id == SurfaceControlId { ControlKind::Pad, 7, 0 } && ev[2].type == SurfaceEventType::Release);
            }
        }

        beginTest("an unterminated SysEx swallows the rest of the buffer and yields no event, a reply on any channel is recognised");
        {
            const Bytes cut { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0x90, 11, 100 };
            expect(decodeInput(cut.data(), cut.size()).empty());
            Bytes reply { 0xF0, 0x7E, 0x00, 0x06, 0x02, 0x00, 0x20, 0x29, 0x03, 0x01, 0x00, 0x00, 0x00, 0x04, 0x02, 0x02, 0xF7 };
            reply[2] = 0x05;
            expect(isInquiryReply(reply.data(), reply.size()));
        }

        beginTest("pad and button messages decode on any MIDI channel");
        {
            const Bytes ch5 { 0x95, 11, 100, 0xB5, 91, 127 };
            expectEquals((int) decodeInput(ch5.data(), ch5.size()).size(), 2);
        }

        beginTest("a black light is palette 0 whatever its mode; the 7-bit scaling hits the ends exactly");
        {
            const auto msgs = lightingMessages({ { 11, LedMode::Flash, 0 } }, false);
            expect(! msgs.empty() && msgs[0] == Bytes { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03, 0x00, 11, 0, 0xF7 });
            const auto white = lightingMessages({ { 11, LedMode::Static, 0xFFFFFF } }, true);
            expect(! white.empty() && white[0][9] == 127 && white[0][10] == 127 && white[0][11] == 127);
        }

        beginTest("RGB lighting message: header, type 3 entries, channels scaled to 0..127");
        {
            const auto msgs = lightingMessages({ { 11, LedMode::Static, 0xFF8000 }, { 98, LedMode::Pulse, 0x000000 }, { 19, LedMode::Static, 0x010203 } }, true);
            expectEquals((int) msgs.size(), 1);
            const Bytes want { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03,
                               0x03, 11, 127, 64, 0,
                               0x03, 98, 0, 0, 0,
                               0x03, 19, 0, 1, 1,
                               0xF7 };
            expect(! msgs.empty() && msgs[0] == want);
        }

        beginTest("a lighting batch holds 81 entries; the 82nd starts a second message");
        {
            std::vector<PadLight> lights;
            for (int i = 0; i < 82; ++i) lights.push_back({ 11 + (i % 8), LedMode::Static, 0x102030u });
            const auto msgs = lightingMessages(lights, true);
            expectEquals((int) msgs.size(), 2);
            if (msgs.size() == 2)
            {
                expectEquals((int) msgs[0].size(), 7 + 81 * 5 + 1);
                expectEquals((int) msgs[1].size(), 7 + 1 * 5 + 1);
                expect(msgs[1].front() == 0xF0 && msgs[1].back() == 0xF7);
            }
            expect(lightingMessages({}, true).empty(), "nothing to light, nothing to send");
        }

        beginTest("palette lighting: static, flash (off / colour) and pulse forms, off is palette 0");
        {
            const auto msgs = lightingMessages({ { 11, LedMode::Static, 0xFF6259 }, { 12, LedMode::Flash, 0x5B5DFD },
                                                 { 13, LedMode::Pulse, 0x4CFF4C }, { 14, LedMode::Pulse, 0 } }, false);
            const Bytes want { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03,
                               0x00, 11, 5,
                               0x01, 12, 0, 45,
                               0x02, 13, 21,
                               0x00, 14, 0,
                               0xF7 };
            expectEquals((int) msgs.size(), 1);
            expect(! msgs.empty() && msgs[0] == want);
        }

        beginTest("nearest palette entry: the confirmed entries map to themselves, dim colours to darker entries");
        expectEquals((int) nearestPaletteIndex(0x000000), 0);
        expectEquals((int) nearestPaletteIndex(0xFF6259), 5);
        expectEquals((int) nearestPaletteIndex(0x4CFF4C), 21);
        expectEquals((int) nearestPaletteIndex(0x5B5DFD), 45);
        expectEquals((int) nearestPaletteIndex(0x4C1D1B), 7, "30 % red lands on the dark red");
        for (const auto& h : kLayerHues)
        {
            bool bright = false, dim = false;
            for (const auto& e : kPalette) { bright = bright || e.index == h.bright; dim = dim || e.index == h.dim; }
            expect(bright && dim, juce::String(h.name) + " names palette entries that exist");
        }
    }
};

static LaunchpadXProtocolTests launchpadXProtocolTests;
