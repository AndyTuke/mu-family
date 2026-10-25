// Stereo parity: an effect given the same signal on both channels must give the same output on both, even while
// a parameter is ramping. The inserts used to advance each smoothed value on channel 0 only, so the right channel
// stepped once per block instead of ramping (and RingMod's two carriers drifted out of phase for good). The inserts
// and the comb filters now share one ramp across the channels; this pins it across block sizes, including 1 sample.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <memory>
#include <vector>

#include "Audio/FX/Insert/SoftClipInsert.h"
#include "Audio/FX/Insert/HardClipInsert.h"
#include "Audio/FX/Insert/FoldInsert.h"
#include "Audio/FX/Insert/ClipperInsert.h"
#include "Audio/FX/Insert/TapeSatInsert.h"
#include "Audio/FX/Insert/OverdriveInsert.h"
#include "Audio/FX/Insert/TapeDelayInsert.h"
#include "Audio/FX/Insert/RingModInsert.h"
#include "Audio/FX/Insert/CompressorLimiterInsert.h"
#include "Audio/FX/Insert/KarplusStrongInsert.h"
#include "Audio/Filters/CombPlusFilter.h"
#include "Audio/Filters/CombMinusFilter.h"
#include "Audio/VoiceParams.h"
#include "Audio/InsertSlotConfig.h"

namespace
{
    constexpr double kSr = 48000.0;
    constexpr int kPhaseLen = 512;           // samples per parameter phase: the new value ramps over the whole second phase
    constexpr int kBlockSizes[] = { 1, 7, 64, 512 };
    constexpr float kTolerance = 1.0e-6f;    // the channels run the same arithmetic, so they should be bit-identical

    // The same deterministic noise on both channels, 1024 samples. Fixed seed, no host randomness.
    juce::AudioBuffer<float> makeIdenticalNoise()
    {
        juce::AudioBuffer<float> b(2, 2 * kPhaseLen);
        juce::Random rng(20261016);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float s = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
            b.setSample(0, i, s);
            b.setSample(1, i, s);
        }
        return b;
    }

    // Largest |L - R| over the buffer and the largest |L| (to show the case was not silent).
    void compareChannels(const juce::AudioBuffer<float>& b, float& maxDiff, float& maxLevel)
    {
        maxDiff = 0.0f; maxLevel = 0.0f;
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            maxDiff  = juce::jmax(maxDiff, std::abs(b.getSample(0, i) - b.getSample(1, i)));
            maxLevel = juce::jmax(maxLevel, std::abs(b.getSample(0, i)));
        }
    }

    void setSlotActual(VoiceParams& p, int slot, float actual)
    {
        p.insertParam[slot] = mu_ui::actualToNorm(actual, p.insertAlgo, slot);
    }

    VoiceParams makeParams(int algo, const float (&actual)[4])
    {
        VoiceParams p;
        p.insertAlgo = algo;
        for (int s = 0; s < 4; ++s) setSlotActual(p, s, actual[s]);
        return p;
    }

    std::unique_ptr<InsertAlgorithmBase> makeInsert(int algo)
    {
        switch (algo)
        {
            case 1:  return std::make_unique<SoftClipInsert>();
            case 2:  return std::make_unique<HardClipInsert>();
            case 3:  return std::make_unique<FoldInsert>();
            case 5:  return std::make_unique<ClipperInsert>();
            case 7:  return std::make_unique<CompressorLimiterInsert>();
            case 9:  return std::make_unique<RingModInsert>();
            case 10: return std::make_unique<TapeSatInsert>();
            case 11: return std::make_unique<KarplusStrongInsert>();
            case 14: return std::make_unique<OverdriveInsert>();
            case 15: return std::make_unique<TapeDelayInsert>();
            default: return nullptr;
        }
    }
}

class StereoParityTest : public juce::UnitTest
{
public:
    StereoParityTest() : juce::UnitTest("Stereo parity: identical L / R in gives identical L / R out while a parameter ramps", "DSP") {}

    void runTest() override
    {
        // Slot values (actual units) for the first phase and for the second, where the ramp is active.
        struct InsertCase { int algo; const char* name; float a[4]; float b[4]; };
        const InsertCase inserts[] = {
            { 1,  "SoftClip",   { 20.0f, 0.0f, 0.0f, 20000.0f },  { 80.0f, -6.0f, 0.0f, 20000.0f } },
            { 2,  "HardClip",   { 20.0f, 0.0f, 0.0f, 20000.0f },  { 80.0f, -6.0f, 0.0f, 20000.0f } },
            { 3,  "Fold",       { 20.0f, 0.0f, 0.0f, 20000.0f },  { 80.0f, -6.0f, 0.0f, 20000.0f } },
            { 5,  "Clipper",    { 80.0f, 0.0f, 0.0f, 20000.0f },  { 30.0f, -6.0f, 0.0f, 20000.0f } },
            { 7,  "Compressor", { 20.0f, 0.0f, 5.0f, 100.0f },    { 70.0f, 6.0f, 2.0f, 400.0f } },
            { 9,  "RingMod",    { 20.0f, 0.0f, 0.0f, 300.0f },    { 90.0f, 0.0f, 0.0f, 1200.0f } },
            { 10, "TapeSat",    { 10.0f, 0.0f, 0.0f, 5000.0f },   { 70.0f, -6.0f, 0.0f, 2000.0f } },
            { 11, "Karplus",    { 0.0f, 1.0f, 50.0f, 5000.0f },   { 7.0f, 2.0f, 70.0f, 3000.0f } },
            { 14, "Overdrive",  { 20.0f, 0.0f, 25.0f, 8000.0f },  { 80.0f, -6.0f, 70.0f, 3000.0f } },
            { 15, "TapeDelay",  { 120.0f, 40.0f, 50.0f, 20.0f },  { 400.0f, 70.0f, 30.0f, 80.0f } },
        };

        for (const auto& c : inserts)
        {
            beginTest(juce::String(c.name) + ": identical inputs give identical outputs at block sizes 1, 7, 64 and 512");
            for (const int bs : kBlockSizes)
            {
                auto algo = makeInsert(c.algo);
                if (algo == nullptr) { expect(false, "no insert for algorithm " + juce::String(c.algo)); continue; }
                algo->prepare(kSr, 512);
                const VoiceParams pa = makeParams(c.algo, c.a), pb = makeParams(c.algo, c.b);

                auto whole = makeIdenticalNoise();
                float gr = 0.0f;
                // Phase 0 runs the first settings, phase 1 the second: the change lands between blocks, so a
                // 15 ms ramp is under way for the whole of phase 1.
                for (int phase = 0; phase < 2; ++phase)
                {
                    const VoiceParams& p = phase == 0 ? pa : pb;
                    for (int start = phase * kPhaseLen; start < (phase + 1) * kPhaseLen; start += bs)
                    {
                        const int n = juce::jmin(bs, (phase + 1) * kPhaseLen - start);
                        float* chans[2] = { whole.getWritePointer(0, start), whole.getWritePointer(1, start) };
                        juce::AudioBuffer<float> view(chans, 2, n);   // a window onto the same memory
                        algo->process(view, n, 2, p, gr);
                    }
                }

                float diff = 0.0f, level = 0.0f;
                compareChannels(whole, diff, level);
                expect(std::isfinite(level) && level > 1.0e-4f, juce::String(c.name) + " block " + juce::String(bs) + ": the render was silent or not finite");
                expect(diff <= kTolerance, juce::String(c.name) + " block " + juce::String(bs) + ": left and right differ by "
                                           + juce::String(diff, 8));
            }
        }

        // The two feedback combs (filter types 8 and 15): resonance and delay change between the phases.
        struct CombCase { const char* name; bool minus; };
        for (const auto& c : { CombCase { "Comb+ (filter type 8)", false }, CombCase { "Comb- (filter type 15)", true } })
        {
            beginTest(juce::String(c.name) + ": identical inputs give identical outputs at block sizes 1, 7, 64 and 512");
            for (const int bs : kBlockSizes)
            {
                CombPlusFilter plus;
                CombMinusFilter minus;
                FilterAlgorithmBase& f = c.minus ? static_cast<FilterAlgorithmBase&>(minus) : static_cast<FilterAlgorithmBase&>(plus);
                f.prepare(kSr, 512, 2);

                auto whole = makeIdenticalNoise();
                for (int phase = 0; phase < 2; ++phase)
                {
                    const float cutoff = phase == 0 ? 220.0f : 440.0f;
                    const float res    = phase == 0 ? 0.3f : 0.8f;
                    for (int start = phase * kPhaseLen; start < (phase + 1) * kPhaseLen; start += bs)
                    {
                        const int n = juce::jmin(bs, (phase + 1) * kPhaseLen - start);
                        float* chans[2] = { whole.getWritePointer(0, start), whole.getWritePointer(1, start) };
                        juce::AudioBuffer<float> view(chans, 2, n);
                        f.process(view, n, 2, cutoff, res);
                    }
                }

                float diff = 0.0f, level = 0.0f;
                compareChannels(whole, diff, level);
                expect(std::isfinite(level) && level > 1.0e-4f, juce::String(c.name) + " block " + juce::String(bs) + ": the render was silent or not finite");
                expect(diff <= kTolerance, juce::String(c.name) + " block " + juce::String(bs) + ": left and right differ by "
                                           + juce::String(diff, 8));
            }
        }
    }
};

static StereoParityTest stereoParityTest;
