#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Plugin/PluginProcessor.h"
#include "UI/Components/MuLookAndFeel.h"
#include "UI/Components/KnobWithLabel.h"
#include "UI/Components/DropdownSelect.h"
#include "UI/ModulatorPanel.h"
#include "Modulation/MuToniModDest.h"
#include "Audio/Scales.h"
#include "Audio/Chords.h"
#include "Audio/AnalogueOsc.h"
#include <memory>
#include <vector>

namespace mu_toni
{

// μ-Toni engine panel — the arpeggiator + analogue-voice controls for the
// selected layer/voice, plus the shared modulator section (identical to the
// other products) as the bottom band. All controls bind to the v{N}_* APVTS
// params via attachments; setLayer(idx) re-binds them + the modulator panel.
class EnginePanel : public juce::Component,
                    private juce::Timer
{
public:
    explicit EnginePanel(PluginProcessor& processor) : proc(processor)
    {
        using LF = MuLookAndFeel;

        // Choice controls (AudioParameterInt-backed dropdowns).
        addCombo("scale", "Scale", scaleItems());
        addCombo("chord", "Chord", chordItems());
        addCombo("rate",  "Rate",  rateItems());
        addCombo("trig",  "Trigger", { "Loop", "MIDI" });
        addCombo("o1w",   "Osc 1", waveItems());
        addCombo("o2w",   "Osc 2", waveItems());
        addCombo("ft",    "Filter", filterItems());

        // Toggles.
        addToggle("leg",  "Legato");
        addToggle("snap", "Snap");

        // Arp knobs (purple = sequencer).
        addKnob("root",  "Root",      LF::knobEuclidean);
        addKnob("roct",  "Octave",    LF::knobEuclidean);
        addKnob("inv",   "Inversion", LF::knobEuclidean);
        addKnob("octs",  "Octaves",   LF::knobEuclidean);
        addKnob("dir",   "Direction", LF::knobEuclidean);
        addKnob("gate",  "Gate",      LF::knobEuclidean);
        addKnob("porta", "Glide",     LF::knobEuclidean);

        // Oscillator / mix knobs.
        addKnob("o1o",   "O1 Oct",  LF::knobEuclidean);
        addKnob("o1f",   "O1 Fine", LF::knobEuclidean);
        addKnob("o1l",   "O1 Lvl",  LF::knobLevel);
        addKnob("o2o",   "O2 Oct",  LF::knobEuclidean);
        addKnob("o2s",   "O2 Semi", LF::knobEuclidean);
        addKnob("o2f",   "O2 Fine", LF::knobEuclidean);
        addKnob("o2l",   "O2 Lvl",  LF::knobLevel);
        addKnob("pw",    "PW",      LF::knobEuclidean);
        addKnob("noise", "Noise",   LF::knobLevel);

        // Filter knobs (teal).
        addKnob("cut",   "Cutoff",  LF::knobPostPad);
        addKnob("res",   "Reso",    LF::knobPostPad);
        addKnob("drv",   "Drive",   LF::knobPostPad);

        // Amp ADSR (amber).
        addKnob("aeA",   "Amp A",   LF::knobLevel);
        addKnob("aeD",   "Amp D",   LF::knobLevel);
        addKnob("aeS",   "Amp S",   LF::knobLevel);
        addKnob("aeR",   "Amp R",   LF::knobLevel);
        addKnob("aeL",   "Amp Lvl", LF::knobLevel);

        // Filter ADSR (teal) + pitch env depth.
        addKnob("feA",   "Flt A",   LF::knobPostPad);
        addKnob("feD",   "Flt D",   LF::knobPostPad);
        addKnob("feS",   "Flt S",   LF::knobPostPad);
        addKnob("feR",   "Flt R",   LF::knobPostPad);
        addKnob("feDep", "Flt Env", LF::knobPostPad);
        addKnob("peDep", "Pch Env", LF::knobModulation);

        // Shared modulator section (bottom band) — mu-core ModulatorPanel + mu-toni dests.
        addAndMakeVisible(modulatorPanel);
        modulatorPanel.setDestProvider(&modDestProvider);

        setLayer(0);
        startTimerHz(30);   // drive the modulator playhead
    }

    ~EnginePanel() override
    {
        stopTimer();
        modulatorPanel.setVoiceSlot(nullptr);   // unbind before the panel/slots die
    }

    void setLayer(int idx)
    {
        currentLayer = juce::jmax(0, idx);
        const juce::String v = "v" + juce::String(currentLayer) + "_";

        for (auto& k : knobs)
            k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
                proc.apvts, v + k.suffix, k.comp->getSlider());
        for (auto& c : combos)
            c.att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
                proc.apvts, v + c.suffix, c.comp->getComboBox());
        for (auto& t : toggles)
            t.att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
                proc.apvts, v + t.suffix, *t.comp);

        modulatorPanel.setVoiceSlot(&proc.voiceSlots[(size_t) currentLayer]);
        repaint();
    }

    int getLayer() const noexcept { return currentLayer; }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(8);

        // Bottom band: the shared modulator section (identical placement to the
        // other products — full width below the engine controls).
        modulatorPanel.setBounds(area.removeFromBottom(juce::jmax(200, area.getHeight() * 45 / 100)));
        area.removeFromBottom(6);

        // Header row: choice controls + toggles.
        auto header = area.removeFromTop(48);
        auto placeCombo = [&](const char* suffix, int w)
        {
            if (auto* c = findCombo(suffix)) c->setBounds(header.removeFromLeft(w).reduced(3, 10));
        };
        placeCombo("scale", 120); placeCombo("chord", 150); placeCombo("rate", 100);
        placeCombo("trig", 90);   placeCombo("ft", 120);
        placeCombo("o1w", 110);   placeCombo("o2w", 110);
        for (auto& t : toggles) t.comp->setBounds(header.removeFromLeft(70).reduced(3, 12));

        area.removeFromTop(6);

        // Knob grid — wrap into rows.
        const int cw = 66, ch = 66, gap = 2;
        int x = area.getX(), y = area.getY();
        for (auto& k : knobs)
        {
            if (x + cw > area.getRight()) { x = area.getX(); y += ch + gap; }
            k.comp->setBounds(x, y, cw, ch);
            x += cw + gap;
        }
    }

private:
    struct KnobDef   { std::unique_ptr<KnobWithLabel>  comp; juce::String suffix;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   att; };
    struct ComboDef  { std::unique_ptr<DropdownSelect> comp; juce::String suffix;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att; };
    struct ToggleDef { std::unique_ptr<juce::ToggleButton> comp; juce::String suffix;
                       std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   att; };

    void addKnob(const char* suffix, const juce::String& label, MuLookAndFeel::ColourIds colour)
    {
        KnobDef d;
        d.comp = std::make_unique<KnobWithLabel>(label, colour);
        d.suffix = suffix;
        d.comp->onStatusUpdate = [this](const juce::String& n, const juce::String& val)
        { if (onStatusUpdate) onStatusUpdate(n, val); };
        addAndMakeVisible(*d.comp);
        knobs.push_back(std::move(d));
    }

    void addCombo(const char* suffix, const juce::String& /*label*/, const juce::StringArray& items)
    {
        ComboDef d;
        d.comp = std::make_unique<DropdownSelect>();
        d.suffix = suffix;
        for (int i = 0; i < items.size(); ++i) d.comp->addItem(items[i], i + 1);
        addAndMakeVisible(*d.comp);
        combos.push_back(std::move(d));
    }

    void addToggle(const char* suffix, const juce::String& label)
    {
        ToggleDef d;
        d.comp = std::make_unique<juce::ToggleButton>(label);
        d.suffix = suffix;
        addAndMakeVisible(*d.comp);
        toggles.push_back(std::move(d));
    }

    DropdownSelect* findCombo(const char* suffix)
    {
        for (auto& c : combos) if (c.suffix == suffix) return c.comp.get();
        return nullptr;
    }

    static juce::StringArray scaleItems()
    {
        juce::StringArray a; for (int i = 0; i < kNumScales; ++i) a.add(kScales[(size_t) i].name); return a;
    }
    static juce::StringArray chordItems()
    {
        juce::StringArray a; for (int i = 0; i < kNumChords; ++i) a.add(kChords[(size_t) i].name); return a;
    }
    static juce::StringArray waveItems()   { return { "Sine", "Triangle", "Saw", "Square", "Pulse" }; }
    static juce::StringArray rateItems()
    {
        return { "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T",
                 "1/16", "1/16.", "1/16T", "1/32", "1/32.", "1/32T" };
    }
    static juce::StringArray filterItems()
    {
        return { "LP12","HP12","BP12","Notch","LP24","HP24","BP24","LP6",
                 "Comb+","AP12","Notch24","HP6","Peak","LoShf","HiShf","Comb-" };
    }

    void timerCallback() override { modulatorPanel.setPlayheadBeat(proc.getInternalBeatPos()); }

public:
    std::function<void(const juce::String&, const juce::String&)> onStatusUpdate;

private:
    PluginProcessor& proc;
    int currentLayer = 0;
    std::vector<KnobDef>   knobs;
    std::vector<ComboDef>  combos;
    std::vector<ToggleDef> toggles;

    ::ModulatorPanel modulatorPanel;
    ModDestProvider  modDestProvider = makeModDestProvider();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnginePanel)
};

} // namespace mu_toni
