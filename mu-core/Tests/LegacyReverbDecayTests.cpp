// A state saved before the reverb Decay knob has no rev_decay. It must load with the Decay that
// sounds as its reverb did then (the old slot took the algorithm's table value as rt20; the new one
// takes decay / 3), not the 2.5 s default, and never the Decay the instance held before the load.
// A state that saves rev_decay keeps it, 2.5 included. Run through a real ProcessorBase: its
// toComposedState / applyComposedState and the listener that routes parameter changes to the slot.

#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

#include "Plugin/MixerFxParams.h"
#include "Persistence/LegacyReverbDecay.h"
#include "Audio/FX/Slots/ReverbSlot.h"
#include "TestProcessorBase.h"

namespace
{
    struct StateProcessor : TestProcessorBase
    {
        static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
        {
            juce::AudioProcessorValueTreeState::ParameterLayout layout;
            mu_mixfx::addGlobalFxParams(layout);
            return layout;
        }
        StateProcessor() : TestProcessorBase(WrapperType::wrapperType_Undefined, makeLayout())
        {
            startFxParamSync();
            initLayerState({});   // every parameter is a global
        }
        using ProcessorBase::toComposedState;
        using ProcessorBase::applyComposedState;
        using ProcessorBase::captureComposedState;

        void setPlain(const char* id, float plain)
        {
            if (auto* prm = apvts.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1(plain));
        }
        float plain(const char* id) const { return apvts.getRawParameterValue(id)->load(); }
    };

    // The rt20 the reverb was given before the Decay knob: the algorithm's table value, as a double.
    constexpr float kOldRt20[] = { 0.6f, 2.5f, 1.8f, 0.35f };

    // A composed state as the instance saves it, with rev_algo set and rev_decay's row removed.
    juce::ValueTree stateWithoutDecay(int algorithm)
    {
        StateProcessor source;
        source.setPlain("rev_algo", (float) algorithm);
        auto state = source.captureComposedState();
        auto globals = state.getChildWithName(mu_pp::kGlobalsTag);
        globals.removeChild(globals.getChildWithProperty("id", mu_pp::kReverbDecayId), nullptr);
        return state;
    }
}

struct LegacyReverbDecayTests : juce::UnitTest
{
    LegacyReverbDecayTests() : juce::UnitTest("A state without rev_decay loads its algorithm's old Decay", "mu-core") {}

    // The loaded slot's rt20 (decay / 3, as ReverbSlot::updateReverb computes it) against the old one.
    double rt20Error(StateProcessor& p, int algorithm)
    {
        const double rt20 = (double) p.fxChain.reverbSlot().getDecay() / 3.0;
        return rt20 - (double) kOldRt20[algorithm];
    }

    void runTest() override
    {
        const char* names[] = { "Room", "Hall", "Plate", "Spring" };

        beginTest("composed state: each algorithm's old tail, whatever Decay the instance held");
        {
            double maxError = 0.0;
            for (int a = 0; a < 4; ++a)
            {
                StateProcessor p;
                p.setPlain("rev_decay", 9.0f);   // the previous preset's Decay must not survive the load
                p.applyComposedState(p.toComposedState(stateWithoutDecay(a)));

                const auto& rev = p.fxChain.reverbSlot();
                expectEquals(rev.getAlgorithmIndex(), a, juce::String(names[a]) + ": the algorithm loaded");
                expect(std::abs(p.plain("rev_decay") - mu_pp::legacyReverbDecay(a)) <= 1.0e-5f,
                       juce::String(names[a]) + ": rev_decay holds the migrated value exactly (" + juce::String(p.plain("rev_decay"), 9) + ")");
                expect(std::abs(rev.getDecay() - mu_pp::legacyReverbDecay(a)) <= 1.0e-5f, juce::String(names[a]) + ": the slot's decay is the migrated value");

                const double err = rt20Error(p, a);
                maxError = juce::jmax(maxError, std::abs(err));
                logMessage(juce::String(names[a]) + ": decay " + juce::String(rev.getDecay(), 9) + " s, rt20 error " + juce::String(err, 12));
                if (a == 0 || a == 1)
                    expect(std::abs(err) <= 1.0e-6, juce::String(names[a]) + ": rt20 within 1e-6 s of the old value");
                else
                    expect(std::abs(err) <= 1.0e-6, juce::String(names[a]) + ": rt20 within 1e-6 s of the old value");

                // The re-save carries the migrated Decay, and it loads back to the same value.
                StateProcessor again;
                again.applyComposedState(again.toComposedState(p.captureComposedState()));
                expect(std::abs(again.plain("rev_decay") - mu_pp::legacyReverbDecay(a)) <= 1.0e-5f, juce::String(names[a]) + ": the re-save loads back unchanged");
            }
            logMessage("largest rt20 difference to the pre-Decay reverb: " + juce::String(maxError, 12) + " s");
        }

        beginTest("a saved rev_decay is kept, the default value included");
        {
            for (float saved : { 2.5f, 0.1f, 10.0f })
            {
                StateProcessor source;
                source.setPlain("rev_algo", 2.0f);   // Plate: its old Decay would be 5.4
                source.setPlain("rev_decay", saved);
                const auto state = source.captureComposedState();

                StateProcessor p;
                p.setPlain("rev_decay", 9.0f);
                const auto composed = p.toComposedState(state);
                expect(composed == state, "a state that saves rev_decay is returned as it is");
                p.applyComposedState(composed);
                expectWithinAbsoluteError(p.plain("rev_decay"), saved, 1.0e-5f, "the saved Decay loads");
                expectWithinAbsoluteError(p.fxChain.reverbSlot().getDecay(), saved, 1.0e-5f, "and reaches the slot");
            }
        }

        beginTest("rev_algo missing too: the default algorithm's (Hall) old Decay");
        {
            auto state = stateWithoutDecay(2);
            auto globals = state.getChildWithName(mu_pp::kGlobalsTag);
            globals.removeChild(globals.getChildWithProperty("id", mu_pp::kReverbAlgoId), nullptr);
            StateProcessor p;
            p.setPlain("rev_algo", 3.0f);
            p.applyComposedState(p.toComposedState(state));
            expectEquals(p.fxChain.reverbSlot().getAlgorithmIndex(), 1, "the default algorithm loaded");
            expect(std::abs(p.plain("rev_decay") - mu_pp::legacyReverbDecay(1)) <= 1.0e-5f, "with Hall's old Decay");
        }

        beginTest("an older APVTS dump (<PARAM>) is migrated through the composed rebuild");
        {
            for (int a = 0; a < 4; ++a)
            {
                juce::ValueTree dump("State");
                juce::ValueTree algo("PARAM");
                algo.setProperty("id", mu_pp::kReverbAlgoId, nullptr);
                algo.setProperty("value", a, nullptr);
                dump.appendChild(algo, nullptr);

                StateProcessor p;
                p.setPlain("rev_decay", 9.0f);
                p.applyComposedState(p.toComposedState(dump));
                expectEquals(p.fxChain.reverbSlot().getAlgorithmIndex(), a, "the algorithm loaded");
                expect(std::abs(p.plain("rev_decay") - mu_pp::legacyReverbDecay(a)) <= 1.0e-5f, juce::String(names[a]) + ": the old Decay");
            }
        }

        beginTest("addLegacyReverbDecayParam: a dump's PARAMs, as replaceState reads them");
        {
            StateProcessor p;
            p.setPlain("rev_algo", 3.0f);
            auto* revAlgo = p.apvts.getParameter(mu_pp::kReverbAlgoId);

            juce::ValueTree none("State");   // no rev_algo: replaceState loads the default algorithm (Hall), not the current Spring
            mu_pp::addLegacyReverbDecayParam(none, revAlgo);
            expect((float) (double) none.getChildWithProperty("id", mu_pp::kReverbDecayId).getProperty("value")
                       == mu_pp::legacyReverbDecay(1), "no rev_algo PARAM: the default algorithm's Decay");

            juce::ValueTree plate("State");   // a saved rev_algo
            juce::ValueTree plateAlgo("PARAM");
            plateAlgo.setProperty("id", mu_pp::kReverbAlgoId, nullptr);
            plateAlgo.setProperty("value", 2.0, nullptr);
            plate.appendChild(plateAlgo, nullptr);
            mu_pp::addLegacyReverbDecayParam(plate, revAlgo);
            expect((float) (double) plate.getChildWithProperty("id", mu_pp::kReverbDecayId).getProperty("value")
                       == mu_pp::legacyReverbDecay(2), "a saved rev_algo PARAM: its Decay");

            juce::ValueTree noValue("State");   // a PARAM with no value is the default (Hall)
            juce::ValueTree algo("PARAM");
            algo.setProperty("id", mu_pp::kReverbAlgoId, nullptr);
            noValue.appendChild(algo, nullptr);
            mu_pp::addLegacyReverbDecayParam(noValue, revAlgo);
            expect((float) (double) noValue.getChildWithProperty("id", mu_pp::kReverbDecayId).getProperty("value")
                       == mu_pp::legacyReverbDecay(1), "a value-less rev_algo PARAM: the default algorithm's Decay");

            juce::ValueTree saved("State");   // a saved rev_decay is left alone
            juce::ValueTree decay("PARAM");
            decay.setProperty("id", mu_pp::kReverbDecayId, nullptr);
            decay.setProperty("value", 2.5, nullptr);
            saved.appendChild(decay, nullptr);
            mu_pp::addLegacyReverbDecayParam(saved, revAlgo);
            expectEquals(saved.getNumChildren(), 1, "nothing added");
            expect((double) saved.getChild(0).getProperty("value") == 2.5, "the saved 2.5 kept");
        }
    }
};

static LegacyReverbDecayTests legacyReverbDecayTests;
