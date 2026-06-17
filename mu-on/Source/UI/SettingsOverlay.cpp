#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

namespace mu_on
{

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    // Header bar + Close button are owned by mu_ui::SettingsOverlayBase.

    auto makeFieldLabel = [this](juce::Label& lbl, const juce::String& text)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(12.0f))));
        lbl.setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::labelText));
        lbl.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(lbl);
    };

    // ── Audio — master volume (reads/writes mstr_lvl directly) ─────────────────
    masterVolKnob.setRange(0.0, 1.0, 0.001);
    masterVolKnob.getSlider().setSkewFactorFromMidPoint(0.5);
    masterVolKnob.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v <= 0.0) return "-inf dB";
        return juce::String(20.0 * std::log10(v), 1) + " dB";
    };
    if (auto* raw = proc.apvts.getRawParameterValue("mstr_lvl"))
        masterVolKnob.setValue(*raw, juce::dontSendNotification);
    masterVolKnob.onValueChanged = [this](double v) {
        if (auto* param = proc.apvts.getParameter("mstr_lvl"))
            param->setValueNotifyingHost(param->convertTo0to1((float) v));
    };
    addAndMakeVisible(masterVolKnob);

    // ── Display — UI size (Medium / Large) ─────────────────────────────────────
    makeFieldLabel(uiSizeLabel, "Size");
    addAndMakeVisible(uiSizeCtrl);
    uiSizeCtrl.setSelectedIndex(proc.getUiScale() >= ProcessorBase::kUiScaleLarge ? 1 : 0, false);
    uiSizeCtrl.onChange = [this](int idx)
    {
        proc.setUiScale(idx == 1 ? ProcessorBase::kUiScaleLarge : ProcessorBase::kUiScaleMedium);
    };

    // ── Transport — internal free-running BPM ──────────────────────────────────
    makeFieldLabel(bpmLabel, "Tempo");
    bpmInput.setValue((int) proc.getInternalBpm());
    bpmInput.onChange = [this](int v) { proc.setInternalBpm((double) v); };
    addAndMakeVisible(bpmInput);

    // ── MIDI Clock (standalone only) — slave the beat/tempo to external MIDI clock ──
    if (isStandalone)
    {
        makeFieldLabel(clockSourceLabel, "Source");
        clockSourceDropdown.addItem("Internal", 1);
        clockSourceDropdown.addItem("MIDI In",  2);
        clockSourceDropdown.setSelectedId(proc.getMidiSyncEnabled() ? 2 : 1, false);
        clockSourceDropdown.onChange = [this](int id)
        {
            proc.setMidiSyncEnabled(id == 2);
            updateMidiSyncVisibility();
        };
        addAndMakeVisible(clockSourceDropdown);

        makeFieldLabel(midiMessagesLabel, "Messages");
        midiMessagesDropdown.addItem("Clock only", 1);
        midiMessagesDropdown.addItem("Transport",  2);
        midiMessagesDropdown.addItem("Both",       3);
        midiMessagesDropdown.setSelectedId(proc.getMidiSyncMessages() + 1, false);
        midiMessagesDropdown.onChange = [this](int id) { proc.setMidiSyncMessages(id - 1); };
        addAndMakeVisible(midiMessagesDropdown);

        updateMidiSyncVisibility();
    }
}

void SettingsOverlay::computeLayout()
{
    using mu_ui::s;
    layout.contentX = contentX();
    layout.contentW = contentW();

    const int headH    = s(kSectionHeadH);
    const int groupH   = s(kGroupHeadH);
    const int rowH     = s(kRowH);
    const int sectGap  = s(kSectionGap);
    const int groupGap = s(kGroupGap);

    int y = contentTop();

    // ── General sub-panel (Audio, Display, Transport)
    layout.generalGroupHeader = y;
    y += groupH;

    layout.audioHeader = y;
    y += headH;
    layout.masterVolY  = y;
    y += s(64);                           // tall enough for the rotary knob + label
    y += sectGap;

    layout.displayHeader = y;
    y += headH;
    layout.uiSizeRowY    = y;
    y += rowH;
    y += sectGap;

    layout.transportHeader = y;
    y += headH;
    layout.bpmRowY         = y;
    y += rowH;

    // ── MIDI sub-panel (MIDI Clock — standalone only). Hidden entirely in a DAW
    //    (no MIDI content applies there until the engine adds note / program change).
    if (isStandalone)
    {
        y += groupGap;

        layout.midiGroupHeader = y;
        y += groupH;

        layout.midiClockHeader  = y;
        y += headH;
        layout.clockSourceRowY  = y;
        y += rowH;
        layout.midiMessagesRowY = y;
        y += rowH;
    }
}

void SettingsOverlay::updateMidiSyncVisibility()
{
    // The Messages row only matters when MIDI-clock input is selected.
    const bool on = proc.getMidiSyncEnabled();
    midiMessagesLabel   .setVisible(on);
    midiMessagesDropdown.setVisible(on);
}

void SettingsOverlay::layoutContent()
{
    using mu_ui::s;
    computeLayout();   // Close button + header bar are positioned by the base.

    const int labelX  = rowLabelX();      // indented from the section headings
    const int labelW  = s(kLabelW);
    const int ctrlX   = rowControlX();    // left-aligned control column
    const int ctrlW   = s(kControlW);
    const int rowH    = s(kRowH);
    const int mvW     = s(kMasterVolW);
    const int mvH     = s(kMasterVolH);

    // Audio — Master Vol knob left-aligned at the row indent.
    masterVolKnob.setBounds(labelX, layout.masterVolY, mvW, mvH);

    uiSizeLabel.setBounds(labelX, layout.uiSizeRowY, labelW, rowH);
    uiSizeCtrl .setBounds(ctrlX,  layout.uiSizeRowY, ctrlW,  rowH);

    bpmLabel.setBounds(labelX, layout.bpmRowY, labelW, rowH);
    bpmInput.setBounds(ctrlX,  layout.bpmRowY, s(90), rowH);

    if (isStandalone)
    {
        clockSourceLabel    .setBounds(labelX, layout.clockSourceRowY,  labelW, rowH);
        clockSourceDropdown .setBounds(ctrlX,  layout.clockSourceRowY,  ctrlW,  rowH);
        midiMessagesLabel   .setBounds(labelX, layout.midiMessagesRowY, labelW, rowH);
        midiMessagesDropdown.setBounds(ctrlX,  layout.midiMessagesRowY, ctrlW,  rowH);
    }
}

void SettingsOverlay::paintContent(juce::Graphics& g)
{
    // Background + "Settings" header bar + divider are painted by the base.
    drawGroupHeader(g, layout.generalGroupHeader, "General");
    drawSectionHeader(g, layout.audioHeader,      "Audio");
    drawSectionHeader(g, layout.displayHeader,    "Display");
    drawSectionHeader(g, layout.transportHeader,  "Transport");

    if (isStandalone)
    {
        drawGroupHeader(g, layout.midiGroupHeader,    "MIDI");
        drawSectionHeader(g, layout.midiClockHeader,  "MIDI Clock");
    }
}

} // namespace mu_on
