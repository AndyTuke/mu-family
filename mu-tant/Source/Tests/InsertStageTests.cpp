// mu-tant insert-stage audio harness - exercises the shared mu-core
// InsertProcessor exactly as mu-tant wires it into the per-voice path
// (engine -> insert -> mixer). Confirms None (algo 0) is a passthrough and a
// drive algorithm actually processes + bounds the signal, so the insert DSP is
// linked + working in mu-tant's build.

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include "Audio/InsertProcessor.h"

class InsertStageTest : public juce::UnitTest
{
public:
    InsertStageTest() : juce::UnitTest("mu-tant insert stage (audio)", "mu-tant") {}

    static void fillSine(juce::AudioBuffer<float>& b, float amp)
    {
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
        {
            auto* d = b.getWritePointer(ch);
            for (int i = 0; i < b.getNumSamples(); ++i)
                d[i] = amp * std::sin(juce::MathConstants<float>::twoPi * 440.0f
                                      * (float) i / 48000.0f);
        }
    }

    static float maxDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
    {
        float m = 0.0f;
        for (int i = 0; i < a.getNumSamples(); ++i)
            m = std::max(m, std::abs(a.getSample(0, i) - b.getSample(0, i)));
        return m;
    }

    void runTest() override
    {
        const int N = 256;

        beginTest("None (algo 0) passes the signal through unchanged");
        {
            InsertProcessor ins; ins.prepare(48000.0, N);
            juce::AudioBuffer<float> buf(2, N), before(2, N);
            fillSine(buf, 0.5f); before.makeCopyOf(buf);
            VoiceParams p; p.insertAlgo = 0;
            ins.process(buf, N, 2, p);
            expect(maxDiff(buf, before) < 1.0e-5f, "None must be a bit-clean passthrough");
        }

        beginTest("SoftClip (algo 1) driven hard actually processes + stays bounded");
        {
            InsertProcessor ins; ins.prepare(48000.0, N);
            juce::AudioBuffer<float> buf(2, N), before(2, N);
            fillSine(buf, 0.8f); before.makeCopyOf(buf);
            VoiceParams p;
            p.insertAlgo     = 1;       // SoftClip
            p.insertParam[0] = 1.0f;    // Drive max
            p.insertParam[1] = 1.0f;    // Output 0 dB
            p.insertParam[3] = 1.0f;    // post LPF wide open (so it doesn't filter out the test tone)
            ins.process(buf, N, 2, p);

            float peak = 0.0f;
            for (int i = 0; i < N; ++i) peak = std::max(peak, std::abs(buf.getSample(0, i)));
            expect(maxDiff(buf, before) > 1.0e-3f, "the insert must change the driven signal");
            expect(peak <= 1.1f, "saturated output stays bounded");
        }
    }
};

static InsertStageTest insertStageTestInstance;
