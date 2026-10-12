// Tests for mu_link::LinkShellBackend - the adapter that lets the shared chrome (transport bar,
// settings page, preset flow) drive mu-link through mu_core::ShellBackend. It runs over a real
// AudioServer, brought up headless (no audio device) so blocks can be rendered by hand: play and BPM
// must reach the master clock, the clock source must switch between Internal and MIDI In, the sync
// offset must reach the server, and the preset directory and extension must be mu-link's own.

#include <juce_core/juce_core.h>
#include <vector>
#include "../UI/LinkShellBackend.h"

#ifdef _WIN32

using namespace mu_link;

namespace
{
    // Renders blocks into a throwaway stereo buffer.
    struct Renderer
    {
        explicit Renderer(AudioServer& s, int frames = 256) : server(s), n(frames), left((size_t) frames), right((size_t) frames)
        {
            ptrs[0] = left.data();
            ptrs[1] = right.data();
        }
        void block() { server.renderBlockHeadless(ptrs, 2, n); }
        AudioServer& server;
        int n;
        std::vector<float> left, right;
        float* ptrs[2];
    };

    juce::File freshRoot()
    {
        return juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("mu-link-shell-" + juce::String::toHexString(juce::Random::getSystemRandom().nextInt64()));
    }
}

class LinkShellBackendTest : public juce::UnitTest
{
public:
    LinkShellBackendTest() : juce::UnitTest("mu-link UI / LinkShellBackend") {}

    void runTest() override
    {
        beginTest("it is a standalone app with a fixed 4/4 meter and Clock + Transport messages");
        {
            const auto root = freshRoot();
            AudioServer server;
            LinkShellBackend shell(server, root);
            expect(shell.isStandaloneApp());
            int num = 0, den = 0;
            shell.setTimeSignature(3, 8);   // ignored
            shell.getTimeSignature(num, den);
            expectEquals(num, 4);
            expectEquals(den, 4);
            expectEquals(shell.getMidiSyncMessages(), 2);
            shell.setMidiSyncMessages(0);
            expectEquals(shell.getMidiSyncMessages(), 2);
            // No host: the defaults hold.
            double ppq = 1.0;
            expect(! shell.isHostPlaying());
            expect(! shell.getHostPpqPosition(ppq));
            root.deleteRecursively();
        }

        beginTest("play and BPM reach the master clock; stopping rewinds");
        {
            const auto root = freshRoot();
            AudioServer server;
            expect(server.startHeadless(48000.0, 256, 120.0), "bus failed");
            LinkShellBackend shell(server, root);
            Renderer r(server);

            expect(! shell.isInternalPlaying());
            shell.setInternalBpm(90.0);
            expectEquals(shell.getInternalBpm(), 90.0);
            shell.toggleInternalPlay();
            expect(shell.isInternalPlaying());
            r.block();                                  // applies the requests at the block start
            r.block();                                  // the second block publishes the advanced position
            auto t = readTransport(server.sharedMemory().transport());
            expectEquals((int) t.playing, 1);
            expectWithinAbsoluteError(t.tempoBpm, 90.0, 0.0001);
            expect(shell.getInternalBeatPos() > 0.0, "beat position should advance while playing");

            // Stop: the next block holds, then the position is back at the top.
            shell.toggleInternalPlay();
            r.block();
            expect(! shell.isInternalPlaying());
            r.block();
            expectEquals(shell.getInternalBeatPos(), 0.0);
            t = readTransport(server.sharedMemory().transport());
            expectEquals((int) t.playing, 0);

            // BPM is clamped to the family range.
            shell.setInternalBpm(1000.0);
            expectEquals(shell.getInternalBpm(), 300.0);
            shell.setInternalBpm(1.0);
            expectEquals(shell.getInternalBpm(), 20.0);
            root.deleteRecursively();
        }

        beginTest("a held nudge bends the tempo by the family percentage and release restores it");
        {
            const auto root = freshRoot();
            AudioServer server;
            expect(server.startHeadless(48000.0, 256, 120.0), "bus failed");
            LinkShellBackend shell(server, root);
            Renderer r(server);
            shell.setInternalBpm(100.0);
            shell.setNudgeDirection(1);
            r.block();
            expectWithinAbsoluteError(readTransport(server.sharedMemory().transport()).tempoBpm, 104.0, 0.0001);
            shell.setNudgeDirection(-1);
            r.block();
            expectWithinAbsoluteError(readTransport(server.sharedMemory().transport()).tempoBpm, 96.0, 0.0001);
            shell.setNudgeDirection(0);
            r.block();
            expectWithinAbsoluteError(readTransport(server.sharedMemory().transport()).tempoBpm, 100.0, 0.0001);
            root.deleteRecursively();
        }

        beginTest("clock source switches between Internal and MIDI In");
        {
            const auto root = freshRoot();
            AudioServer server;
            LinkShellBackend shell(server, root);
            double fakeNow = 1000.0;
            shell.nowMs = [&fakeNow] { return fakeNow; };

            expect(! shell.getMidiSyncEnabled());
            expect(server.clockSourceMode() == ClockSource::Internal);
            expect(shell.getMidiClockState() == MidiClockSync::ClockState::Off);

            shell.setMidiSyncEnabled(true);
            expect(shell.getMidiSyncEnabled());
            expect(server.clockSourceMode() == ClockSource::ExternalMidi);
            expect(shell.getMidiClockState() == MidiClockSync::ClockState::Waiting);   // no pulse yet

            // Pulses arrive: Locked; then silence past the watchdog: Lost.
            server.midiClockEstimator().onClockPulse(1.000);
            fakeNow += 10.0;
            expect(shell.getMidiClockState() == MidiClockSync::ClockState::Locked);
            fakeNow += LinkShellBackend::kClockLostMs + 1.0;
            expect(shell.getMidiClockState() == MidiClockSync::ClockState::Lost);
            server.midiClockEstimator().onClockPulse(1.021);
            expect(shell.getMidiClockState() == MidiClockSync::ClockState::Locked);

            // The incoming transport shows through the MIDI getters.
            expect(! shell.isMidiClockPlaying());
            server.midiClockEstimator().onStart();
            expect(shell.isMidiClockPlaying());
            expect(shell.isInternalPlaying());   // under MIDI In the bar's play state is the incoming one

            shell.setMidiSyncEnabled(false);
            expect(! shell.getMidiSyncEnabled());
            expect(server.clockSourceMode() == ClockSource::Internal);
            expect(shell.getMidiClockState() == MidiClockSync::ClockState::Off);
            root.deleteRecursively();
        }

        beginTest("the clock lamp reports Drifting while the phase servo is off its grid, then Locked; Lost wins");
        {
            using S = MidiClockSync::ClockState;
            const auto root = freshRoot();
            AudioServer server;
            expect(server.startHeadless(48000.0, 250, 120.0), "bus failed");
            LinkShellBackend shell(server, root);
            double fakeNow = 1000.0;
            shell.nowMs = [&fakeNow] { return fakeNow; };
            Renderer r(server, 250);                      // a pulse every 4 blocks at 120 BPM

            shell.setMidiSyncEnabled(true);
            auto& est = server.midiClockEstimator();
            est.onStart();
            double tm = 0.0;
            auto pulses = [&](int n)
            {
                for (int p = 0; p < n; ++p)
                {
                    est.onClockPulse(tm); tm += 0.5 / 24.0;
                    for (int b = 0; b < 4; ++b) r.block();
                }
            };
            pulses(200);
            expect(shell.getMidiClockState() == S::Locked, "steady clock is Locked");

            for (int p = 0; p < 6; ++p) { est.onClockPulse(tm); tm += 0.5 / 24.0; }   // jump a quarter beat ahead
            bool drifted = false;
            for (int p = 0; p < 10 && ! drifted; ++p)     // the averaged error builds up over a few pulses
            {
                pulses(1);
                drifted = shell.getMidiClockState() == S::Drifting;
            }
            expect(drifted, "off the grid is Drifting");

            pulses(400);
            expect(shell.getMidiClockState() == S::Locked, "re-aligned is Locked again");

            // Lost takes precedence: the pulses stop while the servo was last reported drifting.
            for (int p = 0; p < 6; ++p) { est.onClockPulse(tm); tm += 0.5 / 24.0; }
            for (int p = 0; p < 10; ++p) pulses(1);
            expect(shell.getMidiClockState() == S::Drifting);
            fakeNow += LinkShellBackend::kClockLostMs + 1.0;
            expect(shell.getMidiClockState() == S::Lost, "no pulses for the watchdog time is Lost, not Drifting");
            root.deleteRecursively();
        }

        beginTest("the sync offset shifts the external clock's pulse alignment by its milliseconds (positive = earlier)");
        {
            // Runs a synthetic 120 BPM external clock (a pulse every 1000 frames at 48 kHz, fed ahead of the
            // block that holds it) through a headless server, and returns the frame at which the master clock
            // reached its 300th pulse, plus the master beat after a long run.
            struct Run { double pulseFrame = 0.0; double beats = 0.0; };
            constexpr int blockFrames = 250;
            constexpr double targetBeat = 300.0 / 24.0;   // the master's 300th clock pulse
            auto runWithOffset = [&](int offsetMs, bool external)
            {
                AudioServer server;
                expect(server.startHeadless(48000.0, blockFrames, 120.0), "bus failed");
                server.setSyncOffsetMs(offsetMs);
                Renderer r(server, blockFrames);
                auto& est = server.midiClockEstimator();
                if (external)
                {
                    server.setClockSource(ClockSource::ExternalMidi);
                    est.onStart();
                }
                else
                    server.setPlaying(true);
                Run run;
                double tm = 0.0;
                std::uint64_t frame = 0;
                for (int pulse = 0; pulse < 400; ++pulse)
                {
                    if (external) { est.onClockPulse(tm); tm += 0.5 / 24.0; }
                    for (int b = 0; b < 4; ++b)
                    {
                        const double before = server.serverEngine().transportClock().beats();
                        server.renderBlockHeadless(r.ptrs, 2, blockFrames);
                        const double after = server.serverEngine().transportClock().beats();
                        if (before < targetBeat && after >= targetBeat)   // interpolate the crossing inside the block
                            run.pulseFrame = (double) frame + (targetBeat - before) / (after - before) * blockFrames;
                        frame += blockFrames;
                    }
                }
                run.beats = server.serverEngine().transportClock().beats();
                return run;
            };

            const auto base  = runWithOffset(0, true);
            const auto late  = runWithOffset(-40, true);
            const auto early = runWithOffset(40, true);
            const auto max   = runWithOffset(mu_core::ShellBackend::kSyncOffsetMaxMs, true);

            // 40 ms at 120 BPM is 0.08 beat = 1920 frames; positive = the master plays earlier.
            expectWithinAbsoluteError(base.pulseFrame - early.pulseFrame, 1920.0, 12.0, "+40 ms puts pulses 1920 frames earlier");
            expectWithinAbsoluteError(late.pulseFrame - base.pulseFrame,  1920.0, 12.0, "-40 ms puts pulses 1920 frames later");
            expectWithinAbsoluteError(early.beats - base.beats, 0.08, 0.001, "the master beat leads by the offset");
            expectWithinAbsoluteError(max.beats - base.beats, 0.5, 0.002, "250 ms is half a beat at 120 BPM");

            // The offset is for the external clock only: the internal master ignores it.
            const auto internalZero   = runWithOffset(0, false);
            const auto internalOffset = runWithOffset(40, false);
            expectWithinAbsoluteError(internalOffset.beats, internalZero.beats, 1.0e-9, "internal clock unaffected");
        }

        beginTest("the sync offset reaches the server, is clamped and survives a restart");
        {
            const auto root = freshRoot();
            {
                AudioServer server;
                LinkShellBackend shell(server, root);
                expectEquals(shell.getSyncOffsetMs(), 0);
                shell.setSyncOffsetMs(40);
                expectEquals(server.syncOffsetMs(), 40);
                expectEquals(shell.getSyncOffsetMs(), 40);
                shell.setSyncOffsetMs(1000);
                expectEquals(shell.getSyncOffsetMs(), mu_core::ShellBackend::kSyncOffsetMaxMs);
                shell.setSyncOffsetMs(-1000);
                expectEquals(shell.getSyncOffsetMs(), mu_core::ShellBackend::kSyncOffsetMinMs);
                shell.setSyncOffsetMs(25);
            }
            {
                AudioServer server;
                LinkShellBackend shell(server, root);
                expectEquals(shell.getSyncOffsetMs(), 25);
                expectEquals(server.syncOffsetMs(), 25);
            }
            root.deleteRecursively();
        }

        beginTest("the display scale is kept in mu-link's own settings and reported to the shell");
        {
            const auto root = freshRoot();
            float reported = 0.0f;
            {
                AudioServer server;
                LinkShellBackend shell(server, root);
                expectEquals(shell.getUiScale(), mu_core::ShellBackend::kUiScaleMedium);
                shell.onUiScaleChanged = [&reported](float s) { reported = s; };
                shell.setUiScale(mu_core::ShellBackend::kUiScaleLarge);
                expectEquals(reported, mu_core::ShellBackend::kUiScaleLarge);
            }
            {
                AudioServer server;
                LinkShellBackend shell(server, root);
                expectEquals(shell.getUiScale(), mu_core::ShellBackend::kUiScaleLarge);
            }
            root.deleteRecursively();
        }

        beginTest("presets live in Documents/TDP/muLink/Presets with the muLink extension");
        {
            AudioServer server;
            LinkShellBackend shell(server);   // the default root
            const auto expected = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                      .getChildFile("TDP").getChildFile("muLink").getChildFile("Presets");
            expectEquals(shell.getPresetsDir().getFullPathName(), expected.getFullPathName());
            expectEquals(shell.getFullPresetExtension(), juce::String("muLink"));
            expectEquals(shell.getDefaultPresetFile().getFileName(), juce::String("_default.muLink"));
            expectEquals(shell.getContentDir().getFullPathName(), expected.getParentDirectory().getFullPathName());
        }

        beginTest("load and save go to the window's hooks; categories come from the saved files");
        {
            const auto root = freshRoot();
            AudioServer server;
            LinkShellBackend shell(server, root);

            juce::String error;
            shell.onLoadError = [&error](const juce::String& m) { error = m; };
            shell.loadPreset(root.getChildFile("x.muLink"));
            expect(error.isNotEmpty(), "a load with no window attached is reported");

            juce::File loaded;
            juce::String savedName, savedCategory;
            shell.onLoadPresetFile = [&loaded](const juce::File& f) { loaded = f; };
            shell.onSavePreset = [&](const juce::String& n, const juce::String&, const juce::String& c)
            { savedName = n; savedCategory = c; };
            shell.loadPreset(root.getChildFile("y.muLink"));
            expectEquals(loaded.getFileName(), juce::String("y.muLink"));
            shell.savePreset("Live set", "desc", "Live", false);
            expectEquals(savedName, juce::String("Live set"));
            expectEquals(savedCategory, juce::String("Live"));

            expectEquals(shell.loadCategoryList().size(), 0);
            shell.getPresetsDir().createDirectory();
            for (auto [file, cat] : { std::pair<const char*, const char*>{ "a.muLink", "Live" },
                                           { "b.muLink", "Studio" }, { "c.muLink", "Live" }, { "d.muLink", "" } })
            {
                juce::XmlElement xml("MuLinkPreset");
                xml.setAttribute("presetCategory", cat);
                xml.writeTo(shell.getPresetsDir().getChildFile(file));
            }
            const auto categories = shell.loadCategoryList();
            expectEquals(categories.size(), 2);
            expectEquals(categories.joinIntoString(","), juce::String("Live,Studio"));
            root.deleteRecursively();
        }
    }
};

static LinkShellBackendTest linkShellBackendTest;

#endif // _WIN32
