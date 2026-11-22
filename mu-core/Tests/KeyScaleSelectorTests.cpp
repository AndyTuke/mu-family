#include "UI/Components/KeyScaleSelector.h"
#include "Audio/MusicalScales.h"
#include <juce_audio_processors/juce_audio_processors.h>

// The shared key + scale selector (mu_ui::KeyScaleSelector, KeyScaleAttachment): the phrase it shows, the setters' wrap and
// clamp and their silence, and the attachment's two-way binding for an int parameter (mu-Toni's root) and a choice one (mu-Tant's).
struct KeyScaleSelectorTests : juce::UnitTest
{
    KeyScaleSelectorTests() : juce::UnitTest("Key and scale selector (phrase, setters, APVTS attachment)", "mu-core") {}

    struct StubProcessor : juce::AudioProcessor
    {
        const juce::String getName() const override { return "stub"; }
        void prepareToPlay(double, int) override {}
        void releaseResources() override {}
        void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
        juce::AudioProcessorEditor* createEditor() override { return nullptr; }
        bool hasEditor() const override { return false; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram(int) override {}
        const juce::String getProgramName(int) override { return {}; }
        void changeProgramName(int, const juce::String&) override {}
        void getStateInformation(juce::MemoryBlock&) override {}
        void setStateInformation(const void*, int) override {}
    };

    static juce::AudioProcessorValueTreeState::ParameterLayout layoutWith(bool rootIsChoice)
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        if (rootIsChoice)
            layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ "root", 1 }, "Root",
                       juce::StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 0));
        else
            layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID{ "root", 1 }, "Root", 0, 11, 0));
        juce::StringArray scales;
        for (const auto& s : mu_audio::kScales) scales.add(s.name);
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID{ "scale", 1 }, "Scale", scales, 0));
        return layout;
    }

    void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(30); }

    void runTest() override
    {
        beginTest("the phrase reads the note then the scale name");
        {
            expectEquals(mu_ui::KeyScaleSelector::phrase(0, 0), juce::String("C Major"));
            expectEquals(mu_ui::KeyScaleSelector::phrase(10, 8), juce::String("A# Pentatonic Major"));
            expectEquals(mu_ui::KeyScaleSelector::phrase(2, 99), juce::String("D Chromatic"), "an out-of-range scale clamps to the last");
        }

        beginTest("setKey wraps, setScale clamps, neither calls back");
        {
            mu_ui::KeyScaleSelector sel;
            int calls = 0;
            sel.onKeyChanged = [&](int) { ++calls; };
            sel.onScaleChanged = [&](int) { ++calls; };
            sel.setKey(14);
            expectEquals(sel.getKey(), 2);
            sel.setKey(-1);
            expectEquals(sel.getKey(), 11);
            sel.setScale(-5);
            expectEquals(sel.getScale(), 0);
            sel.setScale(500);
            expectEquals(sel.getScale(), mu_audio::kNumScales - 1);
            expectEquals(calls, 0);
        }

        for (const bool choice : { false, true })
        {
            beginTest(juce::String("the attachment binds both ways to ") + (choice ? "a choice root" : "an int root"));
            StubProcessor proc;
            juce::AudioProcessorValueTreeState apvts(proc, nullptr, "S", layoutWith(choice));
            mu_ui::KeyScaleSelector sel;
            mu_ui::KeyScaleAttachment att(apvts, "root", "scale", sel);

            // Parameter to selector.
            apvts.getParameter("root")->setValueNotifyingHost(apvts.getParameter("root")->convertTo0to1(7.0f));
            apvts.getParameter("scale")->setValueNotifyingHost(apvts.getParameter("scale")->convertTo0to1(2.0f));
            pump();
            expectEquals(sel.getKey(), 7, "the selector follows the root parameter");
            expectEquals(sel.getScale(), 2, "the selector follows the scale parameter");

            // Selector to parameter.
            sel.onKeyChanged(4);
            sel.onScaleChanged(9);
            expectEquals((int) std::lround(apvts.getParameter("root")->convertFrom0to1(apvts.getParameter("root")->getValue())), 4);
            expectEquals((int) std::lround(apvts.getParameter("scale")->convertFrom0to1(apvts.getParameter("scale")->getValue())), 9);
        }
    }
};

static KeyScaleSelectorTests keyScaleSelectorTests;
