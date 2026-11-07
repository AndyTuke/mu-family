#include "Plugin/PluginEditor.h"
#include "Plugin/ParameterLayout.h"
#include "Audio/ToniScales.h"   // mu-toni: the family scale table
#include "Audio/Chords.h"       // mu-toni: the chord library
#include "Persistence/LayerParamIds.h"

namespace mu_toni_midi
{

namespace
{
    juce::String arpId(const char* suffix) { return mu_toni::layerParamId(0, suffix); }

    juce::StringArray scaleItems()  { juce::StringArray a; for (int i = 0; i < mu_toni::kNumScales; ++i) a.add(mu_toni::kScales[(size_t) i].name); return a; }
    juce::StringArray chordItems()  { juce::StringArray a; for (const auto& c : mu_toni::kChords) a.add(c.name); return a; }
    juce::StringArray rateItems()   { return { "1/4","1/4.","1/4T","1/8","1/8.","1/8T","1/16","1/16.","1/16T","1/32","1/32.","1/32T" }; }
    juce::StringArray rootItems()   { return { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" }; }
    juce::StringArray channelItems(bool omni)
    {
        juce::StringArray a;
        if (omni) a.add("Omni");
        for (int i = 1; i <= 16; ++i) a.add(juce::String(i));
        return a;
    }
}

PluginEditor::PluginEditor(PluginProcessor& p)
    // Apply the stored scale before any child component constructs, as EditorShellBase does.
    : AudioProcessorEditor((mu_ui::scale = juce::jlimit(ProcessorBase::kUiScaleMedium, ProcessorBase::kUiScaleLarge, p.getUiScale()), &p)),
      proc(p),
      transportBar(p)
{
    setLookAndFeel(&lookAndFeel);

    transportBar.setLogoText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Toni MIDI")));
    transportBar.setShowPresetControls(false);
    transportBar.setShowMixerToggle(false);
    transportBar.setShowSettingsButton(false);
    addAndMakeVisible(transportBar);
    addAndMakeVisible(statusBar);

    aboutOverlay.setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc")) + "-Toni MIDI",
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 AGPLv3")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
        });
    aboutOverlay.onDismiss = [this] { aboutOverlay.setVisible(false); };
    addChildComponent(aboutOverlay);
    aboutOverlay.setUpdateCheck(true, mu_core::update_check::isEnabled(proc.getAppSettingsFile()));
    aboutOverlay.onUpdateCheckChanged = [this](bool on)
    {
        mu_core::update_check::setEnabled(proc.getAppSettingsFile(), on);
        if (proc.onUpdateCheckToggled) proc.onUpdateCheckToggled();
    };
    updateNotice.start();
    transportBar.onLogoClicked = [this] { aboutOverlay.setVisible(true); aboutOverlay.toFront(false); };

    // The arp's choices, then its amounts and switches. `firstValue` is the parameter value the first item stands for.
    addChoice(arpId("scale"), "Scale",   scaleItems(),   0);
    addChoice(arpId("chord"), "Chord",   chordItems(),   0);
    addChoice(arpId("rate"),  "Rate",    rateItems(),    0);
    addChoice(arpId("trig"),  "Trigger", { "Loop", "MIDI" }, 0);
    addChoice(arpId("root"),  "Root",    rootItems(),    0);
    addChoice(kMidiInChId,    "MIDI In", channelItems(true),  0);
    addChoice(kMidiOutChId,   "MIDI Out", channelItems(false), 1);

    using LF = MuLookAndFeel;
    addAmount(arpId("roct"),   "Octave",    0.0,   8.0,   1.0, LF::knobEuclidean);
    addAmount(arpId("inv"),    "Inversion", -4.0,  4.0,   1.0, LF::knobEuclidean);
    addAmount(arpId("octs"),   "Octaves",   1.0,   4.0,   1.0, LF::knobEuclidean);
    addAmount(arpId("dir"),    "Direction", -100.0, 100.0, 1.0, LF::knobEuclidean);
    addAmount(arpId("gate"),   "Gate",      1.0,   100.0, 1.0, LF::knobEuclidean);
    addAmount(arpId("acc"),    "Accent",    0.0,   100.0, 1.0, LF::knobEuclidean);
    addAmount(arpId("accLen"), "Steps",     1.0,   16.0,  1.0, LF::knobEuclidean);
    addAmount(kMidiVelId,      "Velocity",  1.0,   127.0, 1.0, LF::knobLevel);

    addSwitch(arpId("leg"),  "Legato");
    addSwitch(arpId("snap"), "Snap");

    modulatorPanel.setDestProvider(&destProvider);
    modulatorPanel.setLayer(&proc.getCore().getLayer());
    addAndMakeVisible(modulatorPanel);

    sizeDropdown.setStatusName("Size");
    sizeDropdown.addItem("Normal", 1);
    sizeDropdown.addItem("Large",  2);
    sizeDropdown.setSelectedId(proc.getUiScale() >= ProcessorBase::kUiScaleLarge ? 2 : 1, false);
    sizeDropdown.onChange = [this](int id) { proc.setUiScale(id == 2 ? ProcessorBase::kUiScaleLarge : ProcessorBase::kUiScaleMedium); };
    addAndMakeVisible(sizeDropdown);

    juce::Component::SafePointer<PluginEditor> safeThis(this);
    proc.onUiScaleChanged = [safeThis](float scale)
    {
        if (auto* self = safeThis.getComponent())
        {
            mu_ui::scale = scale;
            self->setSize(mu_ui::s(kWindowW), mu_ui::s(kWindowH));
            self->sendLookAndFeelChange();
            self->resized();
            self->repaint();
        }
    };

    setSize(mu_ui::s(kWindowW), mu_ui::s(kWindowH));
    lookAndFeel.setMetalStyle(true);
    lookAndFeel.setAppAccent(MuLookAndFeel::colour(MuLookAndFeel::appPurple));
    sendLookAndFeelChange();
    startTimerHz(mu_ui::kUiRefreshHz);
}

PluginEditor::~PluginEditor()
{
    stopTimer();
    modulatorPanel.setLayer(nullptr);   // before the editor goes: the panel must not keep a pointer into the core
    proc.onUiScaleChanged = nullptr;
    setLookAndFeel(nullptr);
}

float PluginEditor::valueOf(const juce::String& id) const
{
    if (const auto* v = proc.apvts.getRawParameterValue(id)) return v->load();
    return 0.0f;
}

void PluginEditor::writeValue(const juce::String& id, float value)
{
    if (auto* param = proc.apvts.getParameter(id))
    {
        param->beginChangeGesture();
        param->setValueNotifyingHost(param->convertTo0to1(value));
        param->endChangeGesture();
    }
}

void PluginEditor::addChoice(const juce::String& id, const juce::String& label, const juce::StringArray& items, int firstValue)
{
    auto c = std::make_unique<Choice>();
    c->id = id; c->label = label; c->firstValue = firstValue;
    for (int i = 0; i < items.size(); ++i) c->box.addItem(items[i], i + 1);
    c->box.setSelectedId((int) std::lround(valueOf(id)) - firstValue + 1, false);
    auto* raw = c.get();
    c->box.onChange = [this, raw](int itemId) { writeValue(raw->id, (float) (itemId - 1 + raw->firstValue)); statusBar.showParam(raw->label, raw->box.getText()); };
    c->caption.setText(label, juce::dontSendNotification);
    c->caption.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    c->caption.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(c->box);
    addAndMakeVisible(c->caption);
    choices.push_back(std::move(c));
}

void PluginEditor::addAmount(const juce::String& id, const juce::String& label, double lo, double hi, double step, MuLookAndFeel::ColourIds colourId)
{
    auto a = std::make_unique<Amount>();
    a->id = id;
    a->knob = std::make_unique<KnobWithLabel>(label, colourId);
    a->knob->setRange(lo, hi, step);
    a->knob->setValue((double) valueOf(id), juce::dontSendNotification);
    auto* raw = a.get();
    a->knob->onValueChanged = [this, raw](double v) { writeValue(raw->id, (float) v); };
    a->knob->onStatusUpdate = [this](const juce::String& name, const juce::String& val) { statusBar.showParam(name, val); };
    addAndMakeVisible(*a->knob);
    amounts.push_back(std::move(a));
}

void PluginEditor::addSwitch(const juce::String& id, const juce::String& label)
{
    auto s = std::make_unique<Switch>();
    s->id = id;
    s->button.setButtonText(label);
    s->attach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, id, s->button);
    addAndMakeVisible(s->button);
    switches.push_back(std::move(s));
}

void PluginEditor::timerCallback()
{
    modulatorPanel.setPlayheadBeat(proc.getCore().lastBeat());
    // Host automation and preset loads change parameters behind the controls' backs; pull them in.
    for (auto& c : choices)
    {
        const int want = (int) std::lround(valueOf(c->id)) - c->firstValue + 1;
        if (c->box.getSelectedId() != want) c->box.setSelectedId(want, false);
    }
    for (auto& a : amounts)
    {
        const double want = (double) valueOf(a->id);
        if (std::abs(a->knob->getValue() - want) > 0.0001) a->knob->setValue(want, juce::dontSendNotification);
    }
}

void PluginEditor::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    g.fillAll(MuLookAndFeel::colour(Id::windowBackground));
    const auto content = getLocalBounds().withTrimmedTop(s(kTransportH)).withTrimmedBottom(s(kStatusH));
    g.setColour(MuLookAndFeel::colour(Id::panelBackground));
    g.fillRect(content);
    if (MuLookAndFeel::isMetal(*this))
        MuLookAndFeel::drawAccentPanel(g, content.reduced(2).toFloat(), MuLookAndFeel::appAccent(*this));
}

void PluginEditor::resized()
{
    using mu_ui::s;
    auto area = getLocalBounds();

    auto transportRow = area.removeFromTop(s(kTransportH));
    const int dropW = s(90), dropGap = s(6), dropH = s(24);
    sizeDropdown.setBounds(transportRow.removeFromRight(dropW + dropGap).reduced(dropGap / 2, (s(kTransportH) - dropH) / 2));
    transportBar.setBounds(transportRow);
    statusBar.setBounds(area.removeFromBottom(s(kStatusH)));
    aboutOverlay.setBounds(getLocalBounds());

    auto content = area.reduced(s(MuLookAndFeel::kSpaceL));

    // Row 1: the choices, each a small caption over its dropdown, in equal columns.
    auto choiceRow = content.removeFromTop(s(52));
    const int n = (int) choices.size();
    const int colW = n > 0 ? choiceRow.getWidth() / n : 0;
    for (auto& c : choices)
    {
        auto col = choiceRow.removeFromLeft(colW).reduced(s(4), 0);
        c->caption.setBounds(col.removeFromTop(s(18)));
        c->box.setBounds(col.removeFromTop(s(26)));
    }

    // Row 2: the amounts as knobs, then the two switches at the end.
    content.removeFromTop(s(MuLookAndFeel::kSpaceL));
    auto knobRow = content.removeFromTop(s(MuLookAndFeel::kKnobSize1H));
    const int switchW = s(90);
    auto switchCol = knobRow.removeFromRight(switchW);
    const int m = (int) amounts.size();
    const int kw = m > 0 ? knobRow.getWidth() / m : 0;
    for (auto& a : amounts)
        a->knob->setBounds(knobRow.removeFromLeft(kw).withSizeKeepingCentre(juce::jmin(kw, s(MuLookAndFeel::kKnobSize1W)), knobRow.getHeight()));
    const int sh = switchCol.getHeight() / juce::jmax(1, (int) switches.size());
    for (auto& sw : switches)
        sw->button.setBounds(switchCol.removeFromTop(sh).reduced(s(4)));

    // The modulators take the rest of the window.
    content.removeFromTop(s(MuLookAndFeel::kSpaceL));
    modulatorPanel.setBounds(content);
}

} // namespace mu_toni_midi
