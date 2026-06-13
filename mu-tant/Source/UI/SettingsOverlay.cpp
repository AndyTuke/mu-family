#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

namespace mu_tant
{

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p)
{
    // Header bar + Close button are owned by mu_ui::SettingsOverlayBase.

    // identical 4-line label setup is shared by the Size + Tempo rows.
    auto makeFieldLabel = [this](juce::Label& lbl, const juce::String& text)
    {
        lbl.setText(text, juce::dontSendNotification);
        lbl.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(12.0f))));
        lbl.setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::labelText));
        lbl.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(lbl);
    };

    // ── Audio — master volume (reads/writes mstr_lvl directly) ─────────────────
    // Mirrors mu-clid: midpoint skew at 0.5 (= -6 dB) for fader-like resolution +
    // a dB readout. A SliderAttachment can't be used here because it overwrites
    // textFromValueFunction with the parameter's raw formatter (→ "0.0000000000").
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

    // ── MIDI Program Change (Ch 1-8 → voice presets, Ch 9 → full presets) ──────
    midiPresetsBtn.onClick = [this] { if (onMidiPresetsClicked) onMidiPresetsClicked(); };
    fullPresetsBtn.onClick = [this] { if (onFullPresetsClicked) onFullPresetsClicked(); };
    addAndMakeVisible(midiPresetsBtn);
    addAndMakeVisible(fullPresetsBtn);
}

void SettingsOverlay::computeLayout()
{
    using mu_ui::s;
    // Content column geometry is owned by the base (centred + width-capped).
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

    y += groupGap;

    // ── MIDI sub-panel (Program Change)
    layout.midiGroupHeader = y;
    y += groupH;

    layout.midiPCHeader = y;
    y += headH;
    layout.midiPCRowY   = y;
}

void SettingsOverlay::layoutContent()
{
    using mu_ui::s;
    computeLayout();   // Close button + header bar are positioned by the base.

    const int x       = layout.contentX;
    const int cw      = layout.contentW;
    const int labelW  = s(kLabelW);
    const int ctrlGap = s(kLabelCtrlGap);
    const int ctrlX   = x + labelW + ctrlGap;
    const int ctrlW   = s(kControlW);
    const int rowH    = s(kRowH);
    const int mvW     = s(kMasterVolW);
    const int mvH     = s(kMasterVolH);

    // Audio — Master Vol knob centred horizontally within the column.
    masterVolKnob.setBounds(x + (cw - mvW) / 2, layout.masterVolY, mvW, mvH);

    uiSizeLabel.setBounds(x,     layout.uiSizeRowY, labelW, rowH);
    uiSizeCtrl .setBounds(ctrlX, layout.uiSizeRowY, ctrlW,  rowH);

    bpmLabel.setBounds(x,     layout.bpmRowY, labelW, rowH);
    bpmInput.setBounds(ctrlX, layout.bpmRowY, s(90), rowH);

    // MIDI Program Change — two buttons side-by-side, centred within the column.
    const int btnGap = s(8);
    const int btnW   = juce::jmin(s(200), (cw - btnGap) / 2);
    const int totalW = btnW * 2 + btnGap;
    const int btnX   = x + (cw - totalW) / 2;
    midiPresetsBtn.setBounds(btnX,                 layout.midiPCRowY, btnW, rowH);
    fullPresetsBtn.setBounds(btnX + btnW + btnGap, layout.midiPCRowY, btnW, rowH);
}

void SettingsOverlay::paintContent(juce::Graphics& g)
{
    // Background + "Settings" header bar + divider are painted by the base.
    drawGroupHeader(g, layout.generalGroupHeader, "General");
    drawSectionHeader(g, layout.audioHeader,      "Audio");
    drawSectionHeader(g, layout.displayHeader,    "Display");
    drawSectionHeader(g, layout.transportHeader,  "Transport");

    drawGroupHeader(g, layout.midiGroupHeader,    "MIDI");
    drawSectionHeader(g, layout.midiPCHeader,     "MIDI Program Change");
}

} // namespace mu_tant
