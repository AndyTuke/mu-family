#include "SettingsOverlay.h"
#include "Plugin/PluginProcessor.h"

namespace mu_tant
{

SettingsOverlay::SettingsOverlay(PluginProcessor& p)
    : proc(p)
{
    closeBtn.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible(closeBtn);

    auto setupLabel = [this](juce::Label& l, const juce::String& text)
    {
        l.setText(text, juce::dontSendNotification);
        l.setJustificationType(juce::Justification::centredRight);
        l.setFont(juce::Font(juce::FontOptions{}.withHeight(12.0f)));
        l.setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::labelText));
        addAndMakeVisible(l);
    };

    // ── Master volume (bound to the mixer master fader param) ────────────────
    setupLabel(masterVolLabel, "Master Volume");
    addAndMakeVisible(masterVolKnob);
    masterVolAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        proc.apvts, "mstr_lvl", masterVolKnob.getSlider());

    // ── UI size (Medium / Large) ─────────────────────────────────────────────
    setupLabel(uiSizeLabel, "UI Size");
    addAndMakeVisible(uiSizeCtrl);
    uiSizeCtrl.setSelectedIndex(proc.getUiScale() >= ProcessorBase::kUiScaleLarge ? 1 : 0, false);
    uiSizeCtrl.onChange = [this](int idx)
    {
        proc.setUiScale(idx == 1 ? ProcessorBase::kUiScaleLarge : ProcessorBase::kUiScaleMedium);
    };

    // ── Transport BPM ─────────────────────────────────────────────────────────
    setupLabel(bpmLabel, "Tempo (BPM)");
    bpmInput.setValue((int) proc.getInternalBpm());
    bpmInput.onChange = [this](int v) { proc.setInternalBpm((double) v); };
    addAndMakeVisible(bpmInput);

    // ── MIDI Program Change (Ch 1-8 → voice presets, Ch 9 → full presets) ────
    setupLabel(midiPCLabel, "MIDI Prog. Change");
    midiPresetsBtn.onClick = [this] { if (onMidiPresetsClicked) onMidiPresetsClicked(); };
    fullPresetsBtn.onClick = [this] { if (onFullPresetsClicked) onFullPresetsClicked(); };
    addAndMakeVisible(midiPresetsBtn);
    addAndMakeVisible(fullPresetsBtn);
}

void SettingsOverlay::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;
    using mu_ui::sf;

    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    // Header band + title.
    g.setColour(MuLookAndFeel::colour(Id::windowBackground));
    g.fillRect(0, 0, getWidth(), s(kHeaderH));
    g.setColour(MuLookAndFeel::colour(Id::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(18.0f))));
    g.drawText("Settings", s(20), 0, getWidth() - s(40), s(kHeaderH),
               juce::Justification::centredLeft, false);
    g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder));
    g.fillRect(0, s(kHeaderH) - 1, getWidth(), 1);
}

void SettingsOverlay::resized()
{
    using mu_ui::s;
    const int w        = getWidth();
    const int headerH  = s(kHeaderH);
    const int labelW   = s(kLabelW);
    const int ctrlW    = s(kCtrlW);
    const int rowH     = s(kRowH);
    const int rowGap   = s(kRowGap);
    const int gap      = s(12);

    closeBtn.setBounds(w - s(82), (headerH - s(26)) / 2, s(70), s(26));

    // Centred content column. Rows: [right-aligned label] [control].
    const int colW = labelW + gap + ctrlW;
    const int colX = (w - colW) / 2;
    int y = headerH + s(30);

    // Master vol — knob is taller than a row; give it its own taller slot.
    const int knobH = s(MuLookAndFeel::kKnobSize2H);
    const int knobW = s(MuLookAndFeel::kKnobSize2W);
    masterVolLabel.setBounds(colX, y + (knobH - rowH) / 2, labelW, rowH);
    masterVolKnob .setBounds(colX + labelW + gap, y, knobW, knobH);
    y += knobH + rowGap;

    uiSizeLabel.setBounds(colX, y, labelW, rowH);
    uiSizeCtrl .setBounds(colX + labelW + gap, y, ctrlW, rowH);
    y += rowH + rowGap;

    bpmLabel.setBounds(colX, y, labelW, rowH);
    bpmInput.setBounds(colX + labelW + gap, y, s(90), rowH);
    y += rowH + rowGap;

    // MIDI Program Change — label on the left, two buttons sharing the control column.
    midiPCLabel.setBounds(colX, y, labelW, rowH);
    const int btnGap = s(8);
    const int btnW   = (ctrlW - btnGap) / 2;
    midiPresetsBtn.setBounds(colX + labelW + gap,                 y, btnW, rowH);
    fullPresetsBtn.setBounds(colX + labelW + gap + btnW + btnGap, y, btnW, rowH);
}

} // namespace mu_tant
