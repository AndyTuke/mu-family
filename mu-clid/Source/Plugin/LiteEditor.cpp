#include "LiteEditor.h"
#include "Sequencer/Rhythm.h"

LiteEditor::LiteEditor(PluginProcessor& p)
    : AudioProcessorEditor(&p), proc(p),
      transportBar(p), masterLoop(p), euclidPanel(p)
{
    setLookAndFeel(&lookAndFeel);

    // Lite build: keep the master-loop section but drop the preset / mixer
    // chrome (MIDI-effect, no preset library + no mixer overlay).
    transportBar.setLogoText(juce::String(juce::CharPointer_UTF8("\xce\xbc-Clid Lite")));
    transportBar.setLoopSection(&masterLoop, MasterLoopSection::kWidth);
    transportBar.setShowPresetControls(false);
    transportBar.setShowMixerToggle(false);

    addAndMakeVisible(transportBar);
    addAndMakeVisible(rhythmCircle);
    addAndMakeVisible(euclidPanel);
    addAndMakeVisible(statusBar);

    aboutPanel.setProductInfo(
        juce::String(juce::CharPointer_UTF8("\xce\xbc")) + "-Clid Lite",
        juce::StringArray {
            juce::String(juce::CharPointer_UTF8("JUCE \xe2\x80\x94 Proprietary (JUCE 7 license)")),
            juce::String(juce::CharPointer_UTF8("Monocypher \xe2\x80\x94 BSD-2-Clause")),
            juce::String(juce::CharPointer_UTF8("clap-juce-extensions \xe2\x80\x94 MIT")),
            juce::String(juce::CharPointer_UTF8("Bj\xc3\xb6rklund algorithm \xe2\x80\x94 public domain")),
        });
    aboutPanel.onDismiss = [this] { aboutPanel.setVisible(false); };
    addChildComponent(aboutPanel);
    transportBar.onLogoClicked = [this]
    {
        aboutPanel.setVisible(true);
        aboutPanel.toFront(false);
    };

    if (proc.getNumRhythms() > 0)
    {
        const juce::Colour colour { 0xff7F77DD };
        rhythmCircle.setPlayState(&proc.rhythmPlayState[0],
                                  &proc.beatFraction,
                                  &proc.sequencerPlaying,
                                  colour);
        euclidPanel.setRhythm(0);
        euclidPanel.setRhythmColour(colour);
        refreshCircle();
    }

    // Note selector — populated with all 128 MIDI note names, wired to APVTS.
    for (int n = 0; n < 128; ++n)
        noteSelector.addItem(midiNoteName(n), n + 1); // DropdownSelect id = note + 1
    {
        const int initNote = (int)proc.apvts.getRawParameterValue("lite_midiNote")->load();
        noteSelector.setSelectedId(initNote + 1, false);
    }
    noteSelector.onChange = [this](int id)
    {
        const int note = id - 1;
        if (auto* param = proc.apvts.getParameter("lite_midiNote"))
            param->setValueNotifyingHost(param->convertTo0to1((float)note));
    };
    noteSelectorLabel.setText("MIDI Note", juce::dontSendNotification);
    noteSelectorLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    noteSelectorLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(noteSelector);
    addAndMakeVisible(noteSelectorLabel);

    // Accent velocity knob — wired to lite_accentAmt APVTS parameter.
    accentKnob.setRange(0.0, 100.0, 1.0);
    {
        const float initAmt = proc.apvts.getRawParameterValue("lite_accentAmt")->load();
        accentKnob.setValue(initAmt, juce::dontSendNotification);
    }
    accentKnob.onValueChanged = [this](double v)
    {
        if (auto* p = proc.apvts.getParameter("lite_accentAmt"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    accentKnob.onStatusUpdate = [this](const juce::String& name, const juce::String& val)
    {
        statusBar.showParam(name, val);
    };
    addAndMakeVisible(accentKnob);

    euclidPanel.onPatternChanged = [this]() { refreshCircle(); };
    euclidPanel.onStatusUpdate   = [this](const juce::String& name, const juce::String& value)
    {
        statusBar.showParam(name, value);
    };

    // Catch host automation of the Lite-only params so the dropdown + accent
    // knob track DAW automation lanes.
    proc.apvts.addParameterListener("lite_midiNote",  this);
    proc.apvts.addParameterListener("lite_accentAmt", this);

    setSize(760, 420);
}

LiteEditor::~LiteEditor()
{
    proc.apvts.removeParameterListener("lite_accentAmt", this);
    proc.apvts.removeParameterListener("lite_midiNote",  this);
    setLookAndFeel(nullptr);
}

void LiteEditor::parameterChanged(const juce::String& parameterID, float newValue)
{
    juce::Component::SafePointer<LiteEditor> safe(this);
    auto refresh = [safe, parameterID, newValue]
    {
        auto* self = safe.getComponent();
        if (! self) return;
        if (parameterID == "lite_midiNote")
            self->noteSelector.setSelectedId((int) newValue + 1, false);
        else if (parameterID == "lite_accentAmt")
            self->accentKnob.setValue((double) newValue, juce::dontSendNotification);
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        refresh();
    else
        juce::MessageManager::callAsync(std::move(refresh));
}

void LiteEditor::paint(juce::Graphics& g)
{
    g.fillAll(lookAndFeel.findColour(MuLookAndFeel::windowBackground));
}

void LiteEditor::resized()
{
    auto area = getLocalBounds();
    transportBar.setBounds(area.removeFromTop(kTransportH));
    statusBar.setBounds(area.removeFromBottom(kStatusH));

    const int circleSize = juce::jmin(area.getHeight(), kCircleSize);
    rhythmCircle.setBounds(area.removeFromLeft(circleSize)
                               .withSizeKeepingCentre(circleSize, circleSize));
    // Controls row at the bottom of the right panel: note selector + accent knob.
    auto controlsRow = area.removeFromBottom(kControlsH);
    auto accentArea  = controlsRow.removeFromRight(70);
    accentKnob.setBounds(accentArea.reduced(2, 2));
    const int labelW = 70;
    noteSelectorLabel.setBounds(controlsRow.removeFromLeft(labelW).withSizeKeepingCentre(labelW, 22));
    noteSelector.setBounds(controlsRow.withSizeKeepingCentre(controlsRow.getWidth(), 24));

    euclidPanel.setBounds(area);

    aboutPanel.setBounds(getLocalBounds());
}

void LiteEditor::refreshCircle()
{
    if (proc.getNumRhythms() == 0) return;
    const Rhythm& r = proc.getRhythm(0);
    rhythmCircle.setPatterns(r.genA.getStepTypes(),
                             r.genB.getStepTypes(),
                             r.genC.getStepTypes());
}

juce::String LiteEditor::midiNoteName(int note)
{
    static const char* names[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    return juce::String(names[note % 12]) + juce::String(note / 12 - 1);
}
