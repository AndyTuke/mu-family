// LaunchpadXDriver against FakeDevicePorts: open and close sequences, input decoding, the batched lighting
// bytes, and unplug / replug. No hardware.

#include "Devices/LaunchpadX/LaunchpadXDriver.h"
#include "Devices/LaunchpadX/LaunchpadXColourMap.h"
#include "Devices/LaunchpadX/LaunchpadXProtocol.h"
#include "FakeDevicePorts.h"

namespace
{
using namespace mu_control;
using namespace mu_control::test;
using Bytes = std::vector<uint8_t>;

const Bytes kProgrammerOn  { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x01, 0xF7 };
const Bytes kProgrammerOff { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0E, 0x00, 0xF7 };
const Bytes kAftertouchOff { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x0B, 0x02, 0x01, 0xF7 };

// Every control the model would hand over: 64 pads, 8 top, 8 side and the logo.
std::vector<LedState> fullFrame(uint32_t rgb, LedMode mode = LedMode::Static)
{
    std::vector<LedState> f;
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) f.push_back({ { ControlKind::Pad, x, y }, rgb, mode });
    for (int i = 0; i < 8; ++i) f.push_back({ { ControlKind::Top, i, 0 }, rgb, mode });
    for (int i = 0; i < 8; ++i) f.push_back({ { ControlKind::Side, 0, i }, rgb, mode });
    f.push_back({ { ControlKind::Special, 0, 0 }, rgb, mode });
    return f;
}

// The lighting entry the driver sends for a pad index and a screen colour: the colour corrected for the pad, then 7-bit.
Bytes entryFor(uint8_t index, uint32_t screenRgb)
{
    const uint32_t pad = launchpadx::padRgbFor(screenRgb);
    const auto to7 = [](uint32_t c) { return (uint8_t) ((c * 127u + 127u) / 255u); };
    return { 0x03, index, to7((pad >> 16) & 0xFFu), to7((pad >> 8) & 0xFFu), to7(pad & 0xFFu) };
}

// Number of lighting entries in a type 3 message of the given entry width.
int entriesIn(const Bytes& m, int width) { return ((int) m.size() - 8) / width; }
}

class LaunchpadXDriverTests : public juce::UnitTest
{
public:
    LaunchpadXDriverTests() : juce::UnitTest("Launchpad X driver", "MuControl") {}

    void runTest() override
    {
        beginTest("the description is an 8 x 8 RGB surface with 8 top, 8 side and the logo");
        {
            LaunchpadXDriver d;
            const auto& s = d.describe();
            expectEquals(s.gridWidth, 8);
            expectEquals(s.gridHeight, 8);
            expectEquals(s.buttonCount(ButtonKind::Top), 8);
            expectEquals(s.buttonCount(ButtonKind::Side), 8);
            expectEquals(s.buttonCount(ButtonKind::Special), 1);
            expect(s.colour == ColourSupport::Rgb);
            expect(LaunchpadXDriver(false).describe().colour == ColourSupport::Palette);
        }

        beginTest("open: Programmer mode then aftertouch off, on the first output and the second input");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            expect(! d.isOpen());
            expect(d.open(ports));
            expect(d.isOpen() && d.isLinkUp());
            expectEquals((int) ports.sent.size(), 2);
            if (ports.sent.size() == 2) { expect(ports.sent[0] == kProgrammerOn); expect(ports.sent[1] == kAftertouchOff); }
            expectEquals(ports.lastSpec.outputPattern, juce::String("LPX MIDI"));
            expectEquals(ports.lastSpec.inputPattern, juce::String("MIDIIN2 (LPX MIDI)"));
            expect(d.open(ports), "a second open is harmless");
            expectEquals((int) ports.sent.size(), 2);
        }

        beginTest("open fails cleanly when the pad is not there, and sends nothing");
        {
            FakeDevicePorts ports;
            ports.inputs = { "Something else" };
            LaunchpadXDriver d;
            expect(! d.open(ports));
            expect(! d.isOpen() && ports.sent.empty());
            FakeDevicePorts unplugged;
            unplugged.connected = false;
            expect(! d.open(unplugged) && unplugged.sent.empty());
        }

        beginTest("every pad and button press and release becomes a SurfaceEvent with y from the top");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            std::vector<SurfaceEvent> got;
            d.onEvent = [&](const SurfaceEvent& e) { got.push_back(e); };
            expect(d.open(ports));
            int pressed = 0;
            for (const auto& l : fullFrame(0))
            {
                if (l.id.kind == ControlKind::Special) continue;
                for (const bool down : { true, false })
                {
                    got.clear();
                    ports.inject(launchpadx::encodeInput(l.id, down));
                    expectEquals((int) got.size(), 1);
                    if (got.size() == 1)
                        expect(got[0].id == l.id && got[0].type == (down ? SurfaceEventType::Press : SurfaceEventType::Release));
                }
                ++pressed;
            }
            expectEquals(pressed, 80);

            got.clear();
            ports.inject({ 0x90, 11, 100 });   // bottom-left pad = layer 1, clip 1
            ports.inject({ 0xB0, 19, 127 });   // lowest side button
            ports.inject({ 0xB0, 99, 127 });   // the logo
            ports.inject({ 0xA0, 11, 70 });    // polyphonic aftertouch
            expectEquals((int) got.size(), 2);
            if (got.size() == 2)
            {
                expect(got[0].id == SurfaceControlId { ControlKind::Pad, 0, 7 });
                expect(got[1].id == SurfaceControlId { ControlKind::Side, 0, 7 });
            }
        }

        beginTest("a frame goes out as ONE lighting SysEx (80 lights; the logo stays unlit)");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            expect(d.open(ports));
            ports.sent.clear();

            auto frame = fullFrame(0);
            frame[0].rgb  = 0xFF8000;   // top-left pad, bright
            frame[56].rgb = 0x4D2600;   // bottom-left pad, dim (30 %)
            frame[64].rgb = 0x0000FF;   // first top button
            d.setLeds(frame);
            expectEquals((int) ports.sent.size(), 1);
            if (ports.sent.size() == 1)
            {
                const auto& m = ports.sent[0];
                expectEquals(entriesIn(m, 5), 80);
                expect(Bytes(m.begin(), m.begin() + 7) == Bytes { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03 });
                expect(m.back() == 0xF7);
                expect(Bytes(m.begin() + 7, m.begin() + 12) == entryFor(81, 0xFF8000));   // pad row 8 col 1, colour-corrected
                expect(Bytes(m.begin() + 7 + 56 * 5, m.begin() + 12 + 56 * 5) == entryFor(11, 0x4D2600));   // dim orange
                expect(Bytes(m.begin() + 7 + 64 * 5, m.begin() + 12 + 64 * 5) == entryFor(91, 0x0000FF));
            }
        }

        beginTest("only what changed is sent, and an identical frame sends nothing");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            expect(d.open(ports));
            auto frame = fullFrame(0x102030);
            d.setLeds(frame);
            ports.sent.clear();
            d.setLeds(frame);
            expect(ports.sent.empty());
            frame[3].rgb = 0x405060;
            d.setLeds(frame);
            expectEquals((int) ports.sent.size(), 1);
            if (ports.sent.size() == 1)
            {
                expectEquals(entriesIn(ports.sent[0], 5), 1);
                expect(Bytes(ports.sent[0].begin() + 7, ports.sent[0].begin() + 12) == entryFor(84, 0x405060));
            }
        }

        beginTest("RGB lighting ignores the blink mode: a pulse frame is final colours as given");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            expect(d.open(ports));
            ports.sent.clear();
            d.setLeds({ { { ControlKind::Pad, 0, 7 }, 0x808080, LedMode::Pulse } });
            expectEquals((int) ports.sent.size(), 1);
            if (! ports.sent.empty())
                {
                Bytes expected { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03 };
                const auto entry = entryFor(11, 0x808080);   // corrected like any colour, never sent as a blink
                expected.insert(expected.end(), entry.begin(), entry.end());
                expected.push_back(0xF7);
                expect(ports.sent[0] == expected);
            }
        }

        beginTest("a palette driver sends the device's own static, flash and pulse forms");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d(false);
            expect(d.open(ports));
            ports.sent.clear();
            d.setLeds({ { { ControlKind::Pad, 0, 7 }, 0xFF6259, LedMode::Static },
                        { { ControlKind::Pad, 1, 7 }, 0x5B5DFD, LedMode::Flash },
                        { { ControlKind::Pad, 2, 7 }, 0x4CFF4C, LedMode::Pulse } });
            expectEquals((int) ports.sent.size(), 1);
            if (! ports.sent.empty())
                expect(ports.sent[0] == Bytes { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03, 0x00, 11, 5, 0x01, 12, 0, 45, 0x02, 13, 21, 0xF7 });
        }

        beginTest("close turns the pads off, then sends Live mode, and releases the ports");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            expect(d.open(ports));
            d.setLeds(fullFrame(0x102030));
            ports.sent.clear();
            d.close();
            expect(! d.isOpen());
            expectEquals((int) ports.sent.size(), 2);
            if (ports.sent.size() == 2)
            {
                expectEquals(entriesIn(ports.sent[0], 5), 80);
                bool allOff = true;
                for (int i = 0; i < 80; ++i)
                {
                    const auto* e = ports.sent[0].data() + 7 + i * 5;
                    allOff = allOff && e[0] == 0x03 && e[2] == 0 && e[3] == 0 && e[4] == 0;
                }
                expect(allOff, "pads off first");
                expect(ports.sent[1] == kProgrammerOff);
            }
            expectEquals(ports.closeCalls, 1);
            expectEquals(ports.flushCalls, 1);
            expectEquals((int) ports.sentAtFlush, 2);   // both messages were out when flushed
            expectEquals(ports.closesAtFlush, 0);        // and the flush came before the ports closed
            expect(! ports.onBytes && ! ports.onConnectionChanged, "callbacks released");

            ports.sent.clear();
            d.close();
            d.setLeds(fullFrame(0xFFFFFF));
            expect(ports.sent.empty(), "a second close and a closed setLeds send nothing");
        }

        beginTest("the destructor of an open driver sends Live mode");
        {
            FakeDevicePorts ports;
            {
                LaunchpadXDriver d;
                expect(d.open(ports));
                ports.sent.clear();
            }
            expect(! ports.sent.empty() && ports.sent.back() == kProgrammerOff);
            expectEquals(ports.closeCalls, 1);
        }

        beginTest("a driver that never opened sends nothing when destroyed");
        {
            FakeDevicePorts ports;
            { LaunchpadXDriver d; }
            expect(ports.sent.empty() && ports.openCalls == 0);
        }

        beginTest("unplug: lights are held back; replug repeats the setup and the latest whole frame");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            std::vector<SurfaceEvent> got;
            d.onEvent = [&](const SurfaceEvent& e) { got.push_back(e); };
            expect(d.open(ports));
            d.setLeds(fullFrame(0x102030));

            ports.unplug();
            expect(d.isOpen() && ! d.isLinkUp());
            ports.sent.clear();
            auto frame = fullFrame(0x102030);
            frame[0].rgb = 0xFF0000;
            d.setLeds(frame);
            expect(ports.sent.empty(), "nothing is sent to a missing device");

            ports.replug();
            expect(d.isLinkUp());
            expectEquals((int) ports.sent.size(), 3);
            if (ports.sent.size() == 3)
            {
                expect(ports.sent[0] == kProgrammerOn);
                expect(ports.sent[1] == kAftertouchOff);
                expectEquals(entriesIn(ports.sent[2], 5), 80);
                // The refresh walks the controls pads first, top row first, so entry 0 is the red pad.
                expect(Bytes(ports.sent[2].begin() + 7, ports.sent[2].begin() + 12) == Bytes { 0x03, 81, 127, 0, 0 });
            }

            // Input works again, and an unchanged frame after the refresh sends nothing.
            ports.inject(launchpadx::encodeInput({ ControlKind::Top, 0, 0 }, true));
            expectEquals((int) got.size(), 1);
            ports.sent.clear();
            d.setLeds(frame);
            expect(ports.sent.empty());
        }

        beginTest("the replug refresh carries the frame requested while unplugged");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            expect(d.open(ports));
            ports.unplug();
            d.setLeds({ { { ControlKind::Pad, 0, 7 }, 0xFF8000, LedMode::Static } });
            ports.sent.clear();
            ports.replug();
            expectEquals((int) ports.sent.size(), 3);
            if (ports.sent.size() == 3)
            {
                Bytes expected { 0xF0, 0x00, 0x20, 0x29, 0x02, 0x0C, 0x03 };
                const auto entry = entryFor(11, 0xFF8000);
                expected.insert(expected.end(), entry.begin(), entry.end());
                expected.push_back(0xF7);
                expect(ports.sent[2] == expected);
            }
        }

        beginTest("closing while unplugged sends nothing and still releases the ports");
        {
            FakeDevicePorts ports;
            LaunchpadXDriver d;
            expect(d.open(ports));
            ports.unplug();
            ports.sent.clear();
            d.close();
            expect(ports.sent.empty() && ! d.isOpen());
            expectEquals(ports.closeCalls, 1);
            ports.replug();
            expect(ports.sent.empty(), "the closed driver no longer listens for the replug");
        }
    }
};

static LaunchpadXDriverTests launchpadXDriverTests;
