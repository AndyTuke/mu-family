#include <juce_audio_processors/juce_audio_processors.h>
#include "Audio/MixerEngine.h"
#include "Plugin/MixerFxParams.h"

// ChannelState::reset() is what addLayer / resetLayer leave a mixer strip at before the strip's
// parameters are set to their defaults, so it must equal the defaults the shared strip layout
// declares. A drift would leave a fresh or reset layer briefly (or, in a host that ignores the
// parameter writes, permanently) out of step with its knobs.
namespace
{
    struct StubProcessor : juce::AudioProcessor
    {
        const juce::String getName() const override          { return "reset-stub"; }
        void prepareToPlay(double, int) override             {}
        void releaseResources() override                      {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
        bool hasEditor() const override                       { return false; }
        bool acceptsMidi() const override                     { return false; }
        bool producesMidi() const override                    { return false; }
        double getTailLengthSeconds() const override          { return 0.0; }
        int getNumPrograms() override                         { return 1; }
        int getCurrentProgram() override                      { return 0; }
        void setCurrentProgram(int) override                  {}
        const juce::String getProgramName(int) override       { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override   {}
    };
}

struct ChannelStateResetTests : juce::UnitTest
{
    ChannelStateResetTests() : juce::UnitTest("ChannelState reset vs strip defaults", "mu-core") {}

    // Every strip suffix the layout declares; a new parameter must be added here and to reset().
    static const std::vector<juce::String>& suffixes()
    {
        static const std::vector<juce::String> s { "lvl", "pan", "mute", "solo", "sendEff", "sendDly", "sendRev",
                                                   "scSrc", "scAmt", "scAtk", "scRel", "outBus" };
        return s;
    }

    // Compares one channel's reset ChannelState against the APVTS default of every ch{n}_ parameter.
    void checkLayout(const juce::String& label, const mu_mixfx::ChannelStripOptions& opts)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (int c = 0; c < mu_limits::kMaxLayers; ++c)
            mu_mixfx::addChannelStripParams(layout, "ch" + juce::String(c) + "_", "Ch ", opts);
        StubProcessor proc;
        juce::AudioProcessorValueTreeState apvts(proc, nullptr, "ResetState", std::move(layout));

        auto def = [&](int c, const juce::String& s)
        {
            auto* p = apvts.getParameter("ch" + juce::String(c) + "_" + s);
            expect(p != nullptr, label + ": missing parameter ch" + juce::String(c) + "_" + s);
            return p != nullptr ? p->convertFrom0to1(p->getDefaultValue()) : 0.0f;
        };

        // The layout must hold exactly the suffixes this test knows, so a new strip parameter fails here.
        int stripParams = 0;
        for (auto* p : apvts.processor.getParameters())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*>(p))
                if (r->paramID.startsWith("ch0_")) ++stripParams;
        expectEquals(stripParams, (int) suffixes().size(), label + ": strip parameter count changed; extend reset() and this test");

        // Loop: dirty every field, reset, then compare each against the matching parameter default.
        for (int c = 0; c < mu_limits::kMaxLayers; ++c)
        {
            MixerEngine::ChannelState st;
            st.level = 0.3f; st.pan = 0.7f; st.sendEffect = 0.4f; st.sendDelay = 0.5f; st.sendReverb = 0.6f;
            st.panMod = 0.1f; st.sendEffectMod = 0.2f; st.sendDelayMod = 0.3f; st.sendReverbMod = 0.4f;
            st.mute = true; st.solo = true; st.sidechainSource = 3; st.sidechainAmount = 0.9f;
            st.sidechainAttackMs = 77.0f; st.sidechainReleaseMs = 777.0f; st.outputBus = 5;
            st.reset();

            const auto ctx = label + " ch" + juce::String(c);
            expectWithinAbsoluteError(st.level.load(),       def(c, "lvl"),     1.0e-4f, ctx + " level");
            expectWithinAbsoluteError(st.pan.load(),         def(c, "pan"),     1.0e-4f, ctx + " pan");
            expect(st.mute.load() == (def(c, "mute") > 0.5f), ctx + " mute");
            expect(st.solo.load() == (def(c, "solo") > 0.5f), ctx + " solo");
            expectWithinAbsoluteError(st.sendEffect.load(),  def(c, "sendEff"), 1.0e-4f, ctx + " sendEffect");
            expectWithinAbsoluteError(st.sendDelay.load(),   def(c, "sendDly"), 1.0e-4f, ctx + " sendDelay");
            expectWithinAbsoluteError(st.sendReverb.load(),  def(c, "sendRev"), 1.0e-4f, ctx + " sendReverb");
            // The parameter counts 0 = off, 1..8 = channels, 9 = external; the state stores one less.
            expectEquals(st.sidechainSource.load() + 1, juce::roundToInt(def(c, "scSrc")), ctx + " sidechainSource");
            expectWithinAbsoluteError(st.sidechainAmount.load(),    def(c, "scAmt"), 1.0e-4f, ctx + " sidechainAmount");
            expectWithinAbsoluteError(st.sidechainAttackMs.load(),  def(c, "scAtk"), 1.0e-3f, ctx + " sidechainAttackMs");
            expectWithinAbsoluteError(st.sidechainReleaseMs.load(), def(c, "scRel"), 1.0e-3f, ctx + " sidechainReleaseMs");
            expectEquals(st.outputBus.load(), juce::roundToInt(def(c, "outBus")),       ctx + " outputBus");
            for (auto* m : { &st.panMod, &st.sendEffectMod, &st.sendDelayMod, &st.sendReverbMod })
                expect(std::isnan(m->load()), ctx + " a modulated value was not cleared");
        }
    }

    void runTest() override
    {
        beginTest("reset equals the shared strip layout defaults");
        checkLayout("default options", {});

        beginTest("reset equals the defaults for mu-Clid's strip options (no version hint, continuous)");
        {
            mu_mixfx::ChannelStripOptions o;
            o.versionHint = 0;
            o.continuous  = true;
            checkLayout("mu-Clid options", o);
        }
    }
};

static ChannelStateResetTests channelStateResetTests;
