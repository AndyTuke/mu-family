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

        beginTest("Noise: same slot and salt is bit-identical on both channels and across blocks; another slot differs");
        {
            // Noise-only note on a fresh voice seeded for (slot, salt): two consecutive blocks, stereo.
            auto renderNoise = [&](int slot, juce::int64 salt)
            {
                ToniVoice v;
                v.setBank(&bank);
                v.setSlotIndex(slot, salt);
                v.prepare(kSr, kBlock);
                ToniVoiceParams p;
                p.osc1LevelDb = -60.0f;  p.osc2LevelDb = -60.0f;  p.noiseLevelDb = 0.0f;
                v.setParams(p);
                v.noteOn(60);
                juce::AudioBuffer<float> out(2, 2 * kBlock), blk(2, kBlock);
                for (int b = 0; b < 2; ++b)
                {
                    blk.clear();
                    v.process(blk, kBlock);
                    for (int ch = 0; ch < 2; ++ch) out.copyFrom(ch, b * kBlock, blk, ch, 0, kBlock);
                }
                return out;
            };
            auto identical = [&](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y, int len)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < len; ++i)
                        if (x.getSample(ch, i) != y.getSample(ch, i)) return false;
                return true;
            };
            const auto a = renderNoise(1, 0), b = renderNoise(1, 0), other = renderNoise(2, 0);
            expect(a.getMagnitude(0, 0, kBlock) > 0.0f && a.getMagnitude(1, 0, kBlock) > 0.0f, "noise is audible on both channels");
            expect(identical(a, b, 2 * kBlock), "same slot gives bit-identical noise, both channels, both blocks");
            expect(! identical(a, other, kBlock), "different slots give different noise");
            bool repeats = true;   // the second block continues the stream rather than repeating the first
            for (int i = 0; i < kBlock; ++i) repeats = repeats && a.getSample(0, i) == a.getSample(0, kBlock + i);
            expect(! repeats, "the second block continues the sequence, it does not repeat the first");
        }

        beginTest("Noise: differing salts give different noise on the same slot");
        {
            auto firstBlock = [&](int slot, juce::int64 salt)
            {
                ToniVoice v;
                v.setBank(&bank);
                v.setSlotIndex(slot, salt);
                v.prepare(kSr, kBlock);
                ToniVoiceParams p;
                p.osc1LevelDb = -60.0f;  p.osc2LevelDb = -60.0f;  p.noiseLevelDb = 0.0f;
                v.setParams(p);
                v.noteOn(60);
                juce::AudioBuffer<float> blk(2, kBlock);
                blk.clear();
                v.process(blk, kBlock);
                return blk;
            };
            auto same = [&](const juce::AudioBuffer<float>& x, const juce::AudioBuffer<float>& y)
            {
                for (int i = 0; i < kBlock; ++i)
                    if (x.getSample(0, i) != y.getSample(0, i)) return false;
                return true;
            };
            expect(! same(firstBlock(1, 111), firstBlock(1, 222)), "two instances (different salts) on one slot do not share noise");
            expect(same(firstBlock(1, 111), firstBlock(1, 111)), "the same salt repeats");
        }
    }
};

static ToniVoiceTest toniVoiceTest;
