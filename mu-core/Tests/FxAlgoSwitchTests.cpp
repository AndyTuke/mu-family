// Changing the Effect or Reverb algorithm through a parameter (host automation) must leave the slot sounding like the
// knobs say. eff_algo / rev_algo used to call only setAlgorithm: the new algorithm kept its own member defaults while
// the eff_p0..4 / rev_* knobs still held their values. Run on a real ProcessorBase (the family mixer parameter
// layout and the listener that routes parameter changes to the FX slots), driven through the APVTS the way a host does.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

#include "Plugin/MixerFxParams.h"
#include "Audio/FX/Slots/EffectSlot.h"
#include "Audio/FX/Slots/ReverbSlot.h"
#include "TestProcessorBase.h"

namespace
{
    struct FxParamProcessor : TestProcessorBase
    {
        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            mu_mixfx::addGlobalFxParams(layout);
            return layout;
        }
        FxParamProcessor() : TestProcessorBase(WrapperType::wrapperType_Undefined, makeLayout()) { startFxParamSync(); }
    };

    // Sets a parameter to a plain (un-normalised) value through the host-facing path; the listener runs at once.
    void setPlain(FxParamProcessor& p, const char* id, float plain)
    {
        auto* prm = p.apvts.getParameter(id);
        jassert(prm != nullptr);
        if (prm != nullptr) prm->setValueNotifyingHost(prm->convertTo0to1(plain));
    }
}

struct FxAlgoSwitchTests : juce::UnitTest
{
    FxAlgoSwitchTests() : juce::UnitTest("FX algorithm switch by automation pushes the knob values", "mu-core") {}

    void runTest() override
    {
        beginTest("rev_algo: the reverb keeps the knob values, not the new algorithm's preset");
        {
            FxParamProcessor p;
            // Knob values that match no algorithm's preset (the presets are size 0.25 / 0.75 / 0.45 / 0.15, etc.).
            setPlain(p, "rev_size", 0.31f);
            setPlain(p, "rev_pre",  42.0f);
            setPlain(p, "rev_diff", 0.11f);
            setPlain(p, "rev_damp", 0.93f);
            setPlain(p, "rev_mod",  0.47f);

            const auto& rev = p.fxChain.reverbSlot();
            expectWithinAbsoluteError(rev.getSize(), 0.31f, 1.0e-3f, "the size knob reached the slot");

            setPlain(p, "rev_algo", 3.0f);   // Spring: its preset is size 0.15, pre-delay 2, diffusion 0.3, damp 0.8, mod 0.6
            expectEquals(rev.getAlgorithmIndex(), 3, "the algorithm changed");
            expectWithinAbsoluteError(rev.getSize(),      0.31f, 1.0e-3f, "size follows the knob after the switch");
            expectWithinAbsoluteError(rev.getPreDelay(),  42.0f, 1.0e-2f, "pre-delay follows the knob");
            expectWithinAbsoluteError(rev.getDiffusion(), 0.11f, 1.0e-3f, "diffusion follows the knob");
            expectWithinAbsoluteError(rev.getDamp(),      0.93f, 1.0e-3f, "damp follows the knob");
            expectWithinAbsoluteError(rev.getMod(),       0.47f, 1.0e-3f, "mod follows the knob");

            setPlain(p, "rev_algo", 0.0f);   // and back to Room, with the same knobs
            expectEquals(rev.getAlgorithmIndex(), 0, "the algorithm changed again");
            expectWithinAbsoluteError(rev.getSize(), 0.31f, 1.0e-3f, "size still follows the knob");
        }

        beginTest("eff_algo: the new effect plays with the knob values, as if they had been set on it directly");
        {
            constexpr double kSr = 48000.0;
            constexpr int kBlock = 256, kBlocks = 4;
            FxParamProcessor p;
            auto& slot = p.fxChain.effectSlot();
            slot.prepare(kSr, kBlock);

            // Leave Chorus (the default) and set the five knobs while Flanger is active, then come back to Chorus.
            // The knobs now hold values chosen for Flanger's ranges; Chorus must receive them mapped through its own.
            setPlain(p, "eff_algo", 1.0f);
            const float knobs[5] = { 0.8f, 0.9f, 0.2f, 0.1f, 1.0f };
            const char* ids[5] = { "eff_p0", "eff_p1", "eff_p2", "eff_p3", "eff_p4" };
            for (int i = 0; i < 5; ++i) setPlain(p, ids[i], knobs[i]);
            setPlain(p, "eff_algo", 0.0f);
            expectEquals(slot.getAlgorithmIndex(), 0, "Chorus is active again");

            // The reference: a slot on Chorus with the same history as the processor's. The processor seeded Chorus from
            // the default knobs (0.5) when it was built and was prepared after that, so the reference gets the default
            // knobs, is prepared, and only then gets the new knob values through Chorus's ranges. (The smoothers snap to
            // the values held at prepare, so the order decides the first 15 ms of ramp.)
            EffectSlot reference;
            reference.setAlgorithm(0);
            const auto& params = FXAlgorithmRegistry::effectAlgorithms()[0].params;
            for (size_t i = 0; i < params.size() && i < 5; ++i)
                reference.setParam(params[i].id, params[i].minVal + 0.5f * (params[i].maxVal - params[i].minVal));
            reference.prepare(kSr, kBlock);
            for (size_t i = 0; i < params.size() && i < 5; ++i)
                reference.setParam(params[i].id, params[i].minVal + knobs[i] * (params[i].maxVal - params[i].minVal));

            // The same deterministic input through both; they must agree sample for sample. With the old code the
            // processor's chorus kept the default-knob values (rate 4.05 Hz, depth 50 %, ...) instead of the new ones.
            juce::Random rng(424242);
            juce::AudioBuffer<float> a(2, kBlock), b(2, kBlock);
            float maxDiff = 0.0f, maxLevel = 0.0f;
            for (int blk = 0; blk < kBlocks; ++blk)
            {
                for (int i = 0; i < kBlock; ++i)
                {
                    const float s = 0.5f * (rng.nextFloat() * 2.0f - 1.0f);
                    for (int ch = 0; ch < 2; ++ch) { a.setSample(ch, i, s); b.setSample(ch, i, s); }
                }
                slot.processReturn(a);
                reference.processReturn(b);
                for (int i = 0; i < kBlock; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        maxDiff  = juce::jmax(maxDiff,  std::abs(a.getSample(ch, i) - b.getSample(ch, i)));
                        maxLevel = juce::jmax(maxLevel, std::abs(b.getSample(ch, i)));
                    }
            }
            logMessage("max difference to the reference chorus: " + juce::String(maxDiff, 8) + " (level " + juce::String(maxLevel, 3) + ")");
            expect(maxLevel > 1.0e-3f, "the reference chorus produced sound");
            expect(maxDiff < 1.0e-6f, "the slot after the algorithm switch does not match the knobs (max difference " + juce::String(maxDiff, 8) + ")");
        }
    }
};

static FxAlgoSwitchTests fxAlgoSwitchTests;
