#include "TestProcessorCore.h"

// ProcessorCore publishes the host transport for the UI: pollHostTransport() on the audio
// thread, the isHost* / getHost* readers from the message thread.
struct HostTransportPublishTests : juce::UnitTest
{
    HostTransportPublishTests() : juce::UnitTest("ProcessorCore host transport", "mu-core") {}

    void runTest() override
    {
        using Mock = TestProcessorCore::MockPlayHead;

        beginTest("defaults: stopped, 4/4, no position, no bar start");
        {
            TestProcessorCore p;
            double v = 0.0;
            int n = 0, d = 0;
            expect(! p.isHostPlaying());
            p.getHostTimeSignature(n, d);
            expectEquals(n, 4);
            expectEquals(d, 4);
            expect(! p.getHostPpqPosition(v));
            expect(! p.getHostBarStartPpq(v));
        }

        beginTest("playing at ppq 8 is published, then stop");
        {
            TestProcessorCore p;
            Mock ph;
            ph.playing = true;
            ph.ppq = 8.0;
            p.setPlayHead(&ph);
            p.callPoll();
            double v = 0.0;
            expect(p.isHostPlaying());
            expect(p.getHostPpqPosition(v));
            expectEquals(v, 8.0);

            ph.playing = false;
            p.callPoll();
            expect(! p.isHostPlaying());
            p.setPlayHead(nullptr);
        }

        beginTest("host with no ppq: position unavailable, play state still follows");
        {
            TestProcessorCore p;
            Mock ph;
            ph.playing = true;
            ph.hasPpq = false;
            p.setPlayHead(&ph);
            p.callPoll();
            double v = 0.0;
            expect(p.isHostPlaying());
            expect(! p.getHostPpqPosition(v));
            p.setPlayHead(nullptr);
        }

        beginTest("time signature and bar start are published; a huge meter is clamped to 255");
        {
            TestProcessorCore p;
            Mock ph;
            ph.sigNum = 3;
            ph.sigDen = 4;
            ph.hasBar = true;
            ph.barStart = 12.0;
            p.setPlayHead(&ph);
            p.callPoll();
            int n = 0, d = 0;
            double bar = 0.0;
            p.getHostTimeSignature(n, d);
            expectEquals(n, 3);
            expectEquals(d, 4);
            expect(p.getHostBarStartPpq(bar));
            expectEquals(bar, 12.0);

            ph.sigNum = 300;
            p.callPoll();
            p.getHostTimeSignature(n, d);
            expectEquals(n, 255);
            p.setPlayHead(nullptr);
        }

        beginTest("a null playhead after a playing block resets to stopped");
        {
            TestProcessorCore p;
            Mock ph;
            ph.playing = true;
            ph.ppq = 4.0;
            p.setPlayHead(&ph);
            p.callPoll();
            expect(p.isHostPlaying());
            p.setPlayHead(nullptr);
            p.callPoll();
            double v = 0.0;
            expect(! p.isHostPlaying());
            expect(! p.getHostPpqPosition(v));
        }

        beginTest("a bypassed block still publishes the transport");
        {
            TestProcessorCore p;
            Mock ph;
            ph.playing = true;
            ph.ppq = 8.0;
            p.setPlayHead(&ph);
            p.prepareToPlay(48000.0, 64);
            juce::AudioBuffer<float> buffer(2, 64);
            buffer.clear();
            juce::MidiBuffer midi;
            p.processBlockBypassed(buffer, midi);
            double v = 0.0;
            expect(p.isHostPlaying());
            expect(p.getHostPpqPosition(v));
            expectEquals(v, 8.0);
            p.setPlayHead(nullptr);
        }
    }
};

static HostTransportPublishTests hostTransportPublishTests;
