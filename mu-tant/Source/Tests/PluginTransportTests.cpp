// mu-tant plugin transport regression tests.
//
// Root cause: mu-tant's processBlock never read getPlayHead() in plugin
// mode, so blkPlaying was always false. applyGateBlock with playing=false
// returns GateMode::Silence, producing no audio in any DAW host.
//
// These tests guard the contract at two levels:
//   1. readHostTransport() correctly forwards host play state and BPM.
//   2. The transport playing flag directly controls gate output (the path that
//      was broken): playing=false → silence, playing=true+envelopes → audio.
//
// Level 2 is already partially covered by GateStageTests, but repeating the key
// cases here makes the transport→gate dependency explicit in one file.

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>
#include <cmath>
#include <algorithm>

#include "Plugin/HostTransport.h"    // mu-core shared helper under test
#include "Sequencer/GatePattern.h"   // mu-tant gate model

class PluginTransportTest : public juce::UnitTest
{
public:
    PluginTransportTest() : juce::UnitTest("mu-tant plugin transport", "mu-tant") {}

    // ── Mock DAW playhead ────────────────────────────────────────────────────
    struct MockPlayHead : juce::AudioPlayHead
    {
        bool   playing   = false;
        double bpm       = 120.0;
        bool   hasPos    = true;   // false simulates a host that returns no position

        juce::Optional<PositionInfo> getPosition() const override
        {
            if (!hasPos) return {};
            PositionInfo info;
            info.setIsPlaying(playing);
            info.setBpm(bpm);
            return info;
        }
    };

    // ── Helpers ───────────────────────────────────────────────────────────────
    static float peak(const std::vector<float>& v)
    {
        float p = 0.0f;
        for (float s : v) p = std::max(p, std::abs(s));
        return p;
    }

    void runTest() override
    {
        // ── Part 1: readHostTransport unit tests ─────────────────────────────

        beginTest("null playhead → not playing, bpm=0");
        {
            const auto t = mu_core::readHostTransport(nullptr);
            expect(!t.playing, "null playhead must report not playing");
            expectEquals(t.bpm, 0.0, "null playhead must report no BPM");
        }

        beginTest("host playing=true forwards play state and BPM");
        {
            MockPlayHead ph;
            ph.playing = true;
            ph.bpm     = 140.0;
            const auto t = mu_core::readHostTransport(&ph);
            expect(t.playing,             "host playing=true must be forwarded");
            expectEquals(t.bpm, 140.0,    "host BPM must be forwarded");
        }

        beginTest("host playing=false forwards stopped state");
        {
            MockPlayHead ph;
            ph.playing = false;
            ph.bpm     = 130.0;
            const auto t = mu_core::readHostTransport(&ph);
            expect(!t.playing,         "host playing=false must be forwarded");
            expectEquals(t.bpm, 130.0, "BPM is still forwarded when stopped");
        }

        beginTest("host provides no position → not playing, bpm=0");
        {
            MockPlayHead ph;
            ph.hasPos = false;
            const auto t = mu_core::readHostTransport(&ph);
            expect(!t.playing, "empty position must report not playing");
            expectEquals(t.bpm, 0.0, "empty position must report no BPM");
        }

        // ── Part 2: transport→gate contract ──────────────────────────────────
        // Verifies that the playing field returned by readHostTransport feeds
        // applyGateBlock correctly. The bug path: in plugin mode, playing was
        // never true → gate always silenced → no audio in any DAW host.

        using namespace mu_tant;
        constexpr int    N   = 1024;
        constexpr double SR  = 48000.0;
        const     double bps = (120.0 / 60.0) / SR;

        mu_tant::GatePattern pat;
        {
            mu_tant::GateEnvelope env;
            env.startCell   = 0;
            env.lengthCells = 8;   // spans cells 0–7, well within the block
            pat.addEnvelope(env);
        }

        beginTest("gate silent when host says stopped (the regression path)");
        {
            MockPlayHead ph;
            ph.playing = false;
            const auto ht = mu_core::readHostTransport(&ph);
            expect(!ht.playing, "precondition: host is stopped");

            std::vector<float> L(N, 1.0f), R(N, 1.0f);
            applyGateBlock(pat, L.data(), R.data(), N, 0.0f, false,
                           ht.playing, 0.0, bps, SR);
            expectWithinAbsoluteError(peak(L), 0.0f, 1.0e-7f,
                "gate must silence audio when host transport says stopped");
        }

        beginTest("gate passes audio when host says playing");
        {
            MockPlayHead ph;
            ph.playing = true;
            ph.bpm     = 120.0;
            const auto ht = mu_core::readHostTransport(&ph);
            expect(ht.playing, "precondition: host is playing");

            std::vector<float> L(N, 1.0f), R(N, 1.0f);
            applyGateBlock(pat, L.data(), R.data(), N, 0.0f, false,
                           ht.playing, 0.0, (ht.bpm / 60.0) / SR, SR);
            expect(peak(L) > 0.5f,
                "gate must pass audio when host transport says playing");
        }
    }
};

static PluginTransportTest pluginTransportTestInstance;
