// mu-toni voice tests — the wavetable voice (mu-Tant's oscillators via the shared
// XModOscPair) renders a note through every cross-mod mode, stays finite and audible,
// and falls silent after note-off.

#include <juce_core/juce_core.h>
#include "Audio/ToniVoice.h"

using namespace mu_toni;

namespace
{
constexpr double kSr    = 48000.0;
constexpr int    kBlock = 256;

// Render `blocks` blocks of a held note (or its tail) into a stereo buffer; peak level out.
float renderPeak(ToniVoice& v, int blocks, bool& finite)
{
    juce::AudioBuffer<float> buf(2, kBlock);
    float peak = 0.0f;
    for (int b = 0; b < blocks; ++b)
    {
        buf.clear();
        v.process(buf, kBlock);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < kBlock; ++i)
            {
                const float s = buf.getSample(ch, i);
                finite = finite && std::isfinite(s);
                peak = juce::jmax(peak, std::abs(s));
            }
    }
    return peak;
}
} // namespace

class ToniVoiceTest : public juce::UnitTest
{
public:
    ToniVoiceTest() : juce::UnitTest("Wavetable voice", "mu-toni") {}

    void runTest() override
    {
        mu_wavetable::WavetableBank bank;
        bank.loadFactoryBank();

        beginTest("A held note is audible and finite in every cross-mod mode");
        {
            for (int phaseMode = 0; phaseMode < 3; ++phaseMode)
                for (int ampMode = 0; ampMode < 3; ++ampMode)
                {
                    ToniVoice v;
                    v.setBank(&bank);
                    v.prepare(kSr, kBlock);
                    ToniVoiceParams p;
                    p.xmod.phaseMode = phaseMode;  p.xmod.index = 0.6f;
                    p.xmod.ampMode   = ampMode;    p.xmod.depth = 0.5f;  p.xmod.ssbHz = 120.0f;
                    p.xmod.sync      = (phaseMode == 2);  p.xmod.feedback = (ampMode == 1);
                    v.setParams(p);
                    v.noteOn(57);
                    bool finite = true;
                    const float peak = renderPeak(v, 40, finite);
                    expect(finite, "non-finite sample, phase " + juce::String(phaseMode) + " amp " + juce::String(ampMode));
                    expect(peak > 0.01f, "silent, phase " + juce::String(phaseMode) + " amp " + juce::String(ampMode));
                }
        }

        beginTest("Every factory wavetable sounds");
        {
            for (int t = 0; t < bank.numTables(); ++t)
            {
                ToniVoice v;
                v.setBank(&bank);
                v.prepare(kSr, kBlock);
                ToniVoiceParams p;
                p.osc1Table = p.osc2Table = t;
                v.setParams(p);
                v.noteOn(60);
                bool finite = true;
                expect(renderPeak(v, 20, finite) > 0.01f && finite, "table " + bank.tableName(t) + " silent or non-finite");
            }
        }

        beginTest("The voice falls silent after note-off");
        {
            ToniVoice v;
            v.setBank(&bank);
            v.prepare(kSr, kBlock);
            ToniVoiceParams p;
            p.ampR = 0.01f;
            v.setParams(p);
            v.noteOn(60);
            bool finite = true;
            renderPeak(v, 10, finite);
            v.noteOff();
            renderPeak(v, 20, finite);   // release tail (10 ms) has long finished
            expect(! v.isActive(), "voice still active after its release");
        }
    }
};

static ToniVoiceTest toniVoiceTest;
