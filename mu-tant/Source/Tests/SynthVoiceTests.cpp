// mu-tant synth-voice audio tests — render the actual VoiceEngine + NoiseGen
// and assert output is finite, non-silent, and level-responsive. These are the
// "does the synth actually make sound?" smoke tests for the DSP path.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include "Audio/SynthVoice.h"
#include "Audio/WavetableBank.h"

namespace
{
    bool allFinite(const juce::AudioBuffer<float>& b, int n)
    {
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            const auto* d = b.getReadPointer(c);
            for (int i = 0; i < n; ++i)
                if (! std::isfinite(d[i])) return false;
        }
        return true;
    }
    float peakOf(const juce::AudioBuffer<float>& b, int n)
    {
        float p = 0.0f;
        for (int c = 0; c < b.getNumChannels(); ++c)
            p = juce::jmax(p, b.getMagnitude(c, 0, n));
        return p;
    }
}

class SynthVoiceTest : public juce::UnitTest
{
public:
    SynthVoiceTest() : juce::UnitTest("mu-tant synth voice (audio)", "mu-tant") {}

    void runTest() override
    {
        using namespace mu_tant;
        const double sr = 44100.0;
        const int    N  = 512;

        WavetableBank bank;
        bank.generateBuiltIn();

        beginTest("NoiseGen white output stays within [-1, 1]");
        {
            NoiseGen ng; bool ok = true;
            for (int i = 0; i < 20000; ++i)
            {
                const float v = ng.render(NoiseGen::White);
                if (v < -1.0001f || v > 1.0001f) { ok = false; break; }
            }
            expect(ok, "white noise bounded");
        }

        beginTest("NoiseGen pink output is finite + bounded");
        {
            NoiseGen ng; bool ok = true;
            for (int i = 0; i < 20000; ++i)
            {
                const float v = ng.render(NoiseGen::Pink);
                if (! std::isfinite(v) || std::abs(v) > 4.0f) { ok = false; break; }
            }
            expect(ok, "pink noise finite + bounded");
        }

        beginTest("voice renders finite, non-silent output");
        {
            VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
            VoiceConfig c; c.osc1LevelDb = 0.0f; c.levelDb = 0.0f;
            v.setConfig(c);
            juce::AudioBuffer<float> buf(2, N); buf.clear();
            v.process(buf, N);
            expect(allFinite(buf, N), "output finite");
            expect(peakOf(buf, N) > 0.0f, "output non-silent");
        }

        beginTest("slot level: -24 dB is quieter than 0 dB");
        {
            auto render = [&](float levelDb)
            {
                VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
                VoiceConfig c; c.osc1LevelDb = 0.0f; c.levelDb = levelDb;
                v.setConfig(c);
                juce::AudioBuffer<float> buf(2, N); buf.clear();
                v.process(buf, N);
                return peakOf(buf, N);
            };
            expect(render(-24.0f) < render(0.0f), "lower slot level → quieter");
        }

        beginTest("X-Mod FM / AM / Ring all stay finite and non-silent");
        {
            // Test each modulation type at full depth — all should be finite and audible.
            struct Case { float fm; float am; float ring; const char* name; };
            const Case cases[] = {
                { 0.0f,  0.0f,  0.0f,  "Off (no xmod)" },
                { 1.0f,  0.0f,  0.0f,  "FM only" },
                { 0.0f,  1.0f,  0.0f,  "AM only" },
                { 0.0f,  0.0f,  1.0f,  "Ring only" },
                { 1.0f,  1.0f,  1.0f,  "FM+AM+Ring combined" },
            };
            for (const auto& tc : cases)
            {
                VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
                VoiceConfig c;
                c.xmodFm = tc.fm; c.xmodAm = tc.am; c.xmodRing = tc.ring;
                c.osc1LevelDb = 0.0f; c.osc2LevelDb = 0.0f; c.levelDb = 0.0f;
                v.setConfig(c);
                juce::AudioBuffer<float> buf(2, N); buf.clear();
                v.process(buf, N);
                expect(allFinite(buf, N),     juce::String(tc.name) + ": finite");
                expect(peakOf(buf, N) > 0.0f, juce::String(tc.name) + ": non-silent");
            }
        }

        beginTest("hard sync renders finite output with FM cross-mod");
        {
            VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
            VoiceConfig c;
            c.xmodFm = 0.63f; c.sync = true;
            c.osc1LevelDb = 0.0f; c.osc2LevelDb = 0.0f; c.levelDb = 0.0f;
            v.setConfig(c);
            juce::AudioBuffer<float> buf(2, N); buf.clear();
            v.process(buf, N);
            expect(allFinite(buf, N),     "sync + FM: finite");
            expect(peakOf(buf, N) > 0.0f, "sync + FM: non-silent");
        }

        beginTest("AM output changes with depth amount");
        {
            auto renderPeak = [&](float depth)
            {
                VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
                VoiceConfig c;
                c.xmodAm = depth;
                c.osc1LevelDb = 0.0f; c.osc2LevelDb = 0.0f; c.levelDb = 0.0f;
                v.setConfig(c);
                juce::AudioBuffer<float> buf(2, N); buf.clear();
                v.process(buf, N);
                expect(allFinite(buf, N), "AM depth=" + juce::String(depth) + " finite");
                return peakOf(buf, N);
            };
            expect(renderPeak(0.0f) != renderPeak(1.0f), "AM output changes with depth");
        }

        beginTest("Ring Mod output changes with depth amount");
        {
            auto renderPeak = [&](float depth)
            {
                VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
                VoiceConfig c;
                c.xmodRing = depth;
                c.osc1LevelDb = 0.0f; c.osc2LevelDb = 0.0f; c.levelDb = 0.0f;
                v.setConfig(c);
                juce::AudioBuffer<float> buf(2, N); buf.clear();
                v.process(buf, N);
                expect(allFinite(buf, N), "Ring depth=" + juce::String(depth) + " finite");
                return peakOf(buf, N);
            };
            expect(renderPeak(0.0f) != renderPeak(1.0f), "Ring Mod output changes with depth");
        }

        beginTest("noise-only voice (oscs muted) is audible");
        {
            VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
            VoiceConfig c;
            c.osc1LevelDb = -60.0f; c.osc2LevelDb = -60.0f;
            c.noiseLevelDb = 0.0f; c.levelDb = 0.0f;
            v.setConfig(c);
            juce::AudioBuffer<float> buf(2, N); buf.clear();
            v.process(buf, N);
            expect(allFinite(buf, N), "finite");
            expect(peakOf(buf, N) > 0.0f, "noise audible");
        }

        beginTest("process adds into the buffer (does not clear prior content)");
        {
            VoiceEngine v; v.setBank(&bank); v.prepare(sr, N);
            VoiceConfig c; c.osc1LevelDb = 0.0f; c.levelDb = 0.0f;
            v.setConfig(c);
            juce::AudioBuffer<float> buf(2, N);
            for (int ch = 0; ch < 2; ++ch) juce::FloatVectorOperations::fill(buf.getWritePointer(ch), 0.25f, N);
            v.process(buf, N);
            // Output should differ from the constant 0.25 we pre-filled (voice summed in).
            expect(peakOf(buf, N) != 0.25f, "voice summed onto existing content");
            expect(allFinite(buf, N), "finite");
        }
    }
};

static SynthVoiceTest synthVoiceTest;
