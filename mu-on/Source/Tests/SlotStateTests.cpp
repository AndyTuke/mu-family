// The family composed state (mu-core Persistence/LayerState.h): param rows, slot nodes, full
// states and the rebuild of older APVTS-dump states. A minimal headless processor with two
// slots (a_ / b_) and globals stands in for a product.

#include <juce_audio_processors/juce_audio_processors.h>
#include "Persistence/LayerState.h"

namespace
{
class SlotStubProcessor : public juce::AudioProcessor
{
public:
    const juce::String getName() const override          { return "slot-stub"; }
    void prepareToPlay (double, int) override             {}
    void releaseResources() override                      {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    juce::AudioProcessorEditor* createEditor() override   { return nullptr; }
    bool hasEditor() const override                       { return false; }
    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }
    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int) override                 {}
    const juce::String getProgramName (int) override      { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override  {}
};

// Per slot: a 20..20000 Hz cutoff, a choice and a bool; globals: a level and a swing.
juce::AudioProcessorValueTreeState::ParameterLayout layerLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (const char* pre : { "a_", "b_" })
    {
        const juce::String p(pre);
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { p + "cut", 1 }, p + "Cut",
                                                               juce::NormalisableRange<float>(20.0f, 20000.0f), 1000.0f));
        layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { p + "wave", 1 }, p + "Wave",
                                                                juce::StringArray { "Sine", "Saw", "Square" }, 0));
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { p + "on", 1 }, p + "On", true));
    }
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "level", 1 }, "Level", -60.0f, 6.0f, 0.0f));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "seq_swing", 1 }, "Swing", 0.0f, 1.0f, 0.0f));
    return layout;
}

float actual(juce::AudioProcessorValueTreeState& s, const char* id)
{
    auto* p = s.getParameter(id);
    return p->convertFrom0to1(p->getValue());
}
void setActual(juce::AudioProcessorValueTreeState& s, const char* id, float v)
{
    auto* p = s.getParameter(id);
    p->setValueNotifyingHost(p->convertTo0to1(v));
}
} // namespace

class SlotStateTest : public juce::UnitTest
{
public:
    SlotStateTest() : juce::UnitTest("Slot state", "Presets") {}

    void runTest() override
    {
        SlotStubProcessor proc;
        juce::AudioProcessorValueTreeState apvts(proc, nullptr, "StubState", layerLayout());
        const mu_pp::LayerLayout layout(proc, { "a_", "b_" });

        // Extras stand-in: one "colour" property per slot, reset to 0 when a node lacks it.
        std::array<int, 2> colour { 0, 0 };
        const mu_pp::LayerExtras extras {
            [&](int s, juce::ValueTree& n) { n.setProperty("colour", colour[(size_t) s], nullptr); },
            [&](int s, const juce::ValueTree& n) { colour[(size_t) s] = (int) n.getProperty("colour", 0); } };

        beginTest("layout: params split by slot prefix, the rest global");
        {
            expectEquals(layout.numLayers(), 2);
            expectEquals(layout.numParams(0), 3);
            expectEquals(layout.numParams(1), 3);
            expectEquals(layout.numParams(-1), 2);
            expectEquals(layout.slotOf("b_cut"), 1);
            expectEquals(layout.slotOf("seq_swing"), -1);
        }

        beginTest("a slot node: actual values + choice names, loads into the other slot");
        {
            setActual(apvts, "a_cut", 4321.0f);
            setActual(apvts, "a_wave", 2.0f);   // Square
            setActual(apvts, "a_on", 0.0f);
            colour[0] = 5;
            const auto node = mu_pp::captureLayer(layout, extras, 0, "TestLayer");

            const auto cut = node.getChildWithProperty("id", "cut");
            expectWithinAbsoluteError((float) (double) cut.getProperty("x"), 4321.0f, 0.5f);
            expectEquals(node.getChildWithProperty("id", "wave").getProperty("c").toString(), juce::String("Square"));

            mu_pp::applyLayer(layout, extras, 1, node);
            expectWithinAbsoluteError(actual(apvts, "b_cut"), 4321.0f, 0.5f);
            expectWithinAbsoluteError(actual(apvts, "b_wave"), 2.0f, 1e-4f);
            expectWithinAbsoluteError(actual(apvts, "b_on"), 0.0f, 1e-4f);
            expectEquals(colour[1], 5);
        }

        beginTest("rows: a choice is found by name, an older normalised row still reads, a missing one resets");
        {
            juce::ValueTree node("TestLayer");
            juce::ValueTree wave(mu_pp::kRowTag);
            wave.setProperty("id", "wave", nullptr);
            wave.setProperty("x", 0.0, nullptr);       // stale index…
            wave.setProperty("c", "Saw", nullptr);     // …the name wins
            node.appendChild(wave, nullptr);
            juce::ValueTree cut(mu_pp::kRowTag);
            cut.setProperty("id", "cut", nullptr);
            cut.setProperty("v", 0.5, nullptr);        // pre-format-2 normalised row
            node.appendChild(cut, nullptr);

            setActual(apvts, "a_on", 0.0f);
            mu_pp::applyLayer(layout, extras, 0, node);
            expectWithinAbsoluteError(actual(apvts, "a_wave"), 1.0f, 1e-4f);
            expectWithinAbsoluteError(apvts.getParameter("a_cut")->getValue(), 0.5f, 1e-4f);
            expectWithinAbsoluteError(actual(apvts, "a_on"), 1.0f, 1e-4f);   // not in the node → default
            expectEquals(colour[0], 0);                                        // extras reset too
        }

        beginTest("apply: each parameter is written once, an unchanged one not at all");
        {
            setActual(apvts, "a_cut", 5000.0f);
            setActual(apvts, "a_on", 1.0f);   // its default
            struct Counter : juce::AudioProcessorParameter::Listener
            {
                int writes = 0;
                void parameterValueChanged(int, float) override { ++writes; }
                void parameterGestureChanged(int, bool) override {}
            } cut, on;
            apvts.getParameter("a_cut")->addListener(&cut);
            apvts.getParameter("a_on")->addListener(&on);

            juce::ValueTree node("TestLayer");
            juce::ValueTree row(mu_pp::kRowTag);
            row.setProperty("id", "cut", nullptr);
            row.setProperty("x", 300.0, nullptr);
            node.appendChild(row, nullptr);
            layout.applyParams(node, 0);

            expectWithinAbsoluteError(actual(apvts, "a_cut"), 300.0f, 0.5f);
            expectEquals(cut.writes, 1);   // straight to the file value — no detour via the default
            expectEquals(on.writes, 0);    // already at its default
            apvts.getParameter("a_cut")->removeListener(&cut);
            apvts.getParameter("a_on")->removeListener(&on);
        }

        beginTest("full state: globals + every slot round-trip; an empty node resets a slot");
        {
            setActual(apvts, "level", -12.0f);
            setActual(apvts, "b_cut", 222.0f);
            colour = { 3, 4 };
            const auto state = mu_pp::captureState("StubState", layout, extras);
            expect(mu_pp::isComposedState(state), "format 2");

            setActual(apvts, "level", 0.0f);
            setActual(apvts, "b_cut", 1000.0f);
            colour = { 0, 0 };
            mu_pp::applyState(juce::ValueTree::fromXml(*state.createXml()), layout, extras);   // via XML, as on disk
            expectWithinAbsoluteError(actual(apvts, "level"), -12.0f, 1e-3f);
            expectWithinAbsoluteError(actual(apvts, "b_cut"), 222.0f, 0.5f);
            expectEquals(colour[0], 3);
            expectEquals(colour[1], 4);

            mu_pp::applyLayer(layout, extras, 1, {});
            expectWithinAbsoluteError(actual(apvts, "b_cut"), 1000.0f, 0.5f);
            expectEquals(colour[1], 0);
        }

        beginTest("an older APVTS-dump state is rebuilt in the composed shape");
        {
            juce::ValueTree legacy("StubState");
            legacy.setProperty("numVoices", 2, nullptr);
            auto param = [&](const char* id, double v)
            {
                juce::ValueTree p("PARAM");
                p.setProperty("id", id, nullptr);
                p.setProperty("value", v, nullptr);
                legacy.appendChild(p, nullptr);
            };
            param("a_cut", 777.0);
            param("b_wave", 1.0);
            param("level", -3.0);
            param("gone_param", 1.0);   // a parameter the product no longer has
            juce::ValueTree noValue("PARAM");   // APVTS writes these for params it never set
            noValue.setProperty("id", "seq_swing", nullptr);
            legacy.appendChild(noValue, nullptr);
            setActual(apvts, "seq_swing", 0.7f);
            juce::ValueTree data(mu_pp::kChannelDataTag), voice(mu_pp::kChannelNodeTag);
            voice.setProperty("idx", 1, nullptr);
            voice.setProperty("colour", 7, nullptr);
            voice.appendChild(juce::ValueTree("Modulators"), nullptr);
            data.appendChild(voice, nullptr);
            legacy.appendChild(data, nullptr);
            legacy.appendChild(juce::ValueTree("Extra"), nullptr);

            int others = 0;
            const auto composed = mu_pp::composeLegacyState(legacy, layout,
                [&](const juce::ValueTree& child, juce::ValueTree&) { others += child.hasType("Extra") ? 1 : 100; });
            expect(mu_pp::isComposedState(composed), "rebuilt as format 2");
            expectEquals((int) composed.getProperty("numVoices"), 2);
            expectEquals(others, 1);   // only the unknown child is handed back
            const auto slot1 = mu_pp::findLayerNode(composed, 1);
            expectEquals((int) slot1.getProperty("colour"), 7);
            expect(slot1.getChildWithName("Modulators").isValid(), "voice children move into the slot");
            expect(mu_pp::composeLegacyState(composed, layout) == composed, "a composed state passes through");

            colour = { 0, 0 };
            mu_pp::applyState(composed, layout, extras);
            expectWithinAbsoluteError(actual(apvts, "a_cut"), 777.0f, 0.5f);
            expectWithinAbsoluteError(actual(apvts, "b_wave"), 1.0f, 1e-4f);
            expectWithinAbsoluteError(actual(apvts, "level"), -3.0f, 1e-3f);
            expectWithinAbsoluteError(actual(apvts, "seq_swing"), 0.0f, 1e-4f);   // value-less → default, not 0-by-accident
            expectEquals(colour[1], 7);
        }
    }
};

static SlotStateTest slotStateTest;
