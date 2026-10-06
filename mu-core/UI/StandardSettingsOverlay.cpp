#include "StandardSettingsOverlay.h"

namespace mu_ui
{

StandardSettingsOverlay::StandardSettingsOverlay(ProcessorBase& p, Options options)
    : proc(p),
      isStandalone(p.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
{
    // ── Audio — master volume. Reads / writes mstr_lvl directly: a SliderAttachment would
    //    replace the dB readout with the parameter's raw formatter. Midpoint skew at 0.5
    //    (-6 dB) gives fader-like resolution.
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
    general.push_back({ "Audio", 64, [this](const Rows& r) {
        masterVolKnob.setBounds(r.labelX, r.area.getY(),
                                s(MuLookAndFeel::kKnobSize2W), s(MuLookAndFeel::kKnobSize2H)); } });

    // ── Display — UI size.
    makeFieldLabel(uiSizeLabel, "Size");
    uiSizeCtrl.setSelectedIndex(proc.getUiScale() >= ProcessorBase::kUiScaleLarge ? 1 : 0, false);
    uiSizeCtrl.onChange = [this](int idx)
    { proc.setUiScale(idx == 1 ? ProcessorBase::kUiScaleLarge : ProcessorBase::kUiScaleMedium); };
    addAndMakeVisible(uiSizeCtrl);
    // The window resizes live, but text labels pick their font size when created — so a
    // hint below the picker says the plugin needs reopening for those to fully rescale.
    general.push_back({ "Display", 2 * kRowH + kRowGap, [this](const Rows& r) {
        uiSizeLabel.setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
        uiSizeCtrl .setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH); },
        [this](juce::Graphics& g, const Rows& r) {
        drawHint(g, r.area.getY() + r.rowH + s(kRowGap) / 2 + r.rowH / 2,
                 "(reopen the plugin for label fonts to fully rescale)",
                 r.ctrlX, r.area.getRight() - r.ctrlX); } });

    // ── Transport — internal free-running BPM.
    if (options.showTransport)
    {
        makeFieldLabel(bpmLabel, "Tempo");
        bpmInput.setValue((int) proc.getInternalBpm());
        bpmInput.onChange = [this](int v) { proc.setInternalBpm((double) v); };
        addAndMakeVisible(bpmInput);
        general.push_back({ "Transport", kRowH, [this](const Rows& r) {
            bpmLabel.setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
            bpmInput.setBounds(r.ctrlX,  r.area.getY(), s(90),    r.rowH); } });
    }

    // ── MIDI Clock (standalone only) — slave the beat / tempo to external MIDI clock.
    if (isStandalone)
    {
        makeFieldLabel(clockSourceLabel, "Source");
        clockSourceDropdown.addItem("Internal", 1);
        clockSourceDropdown.addItem("MIDI In",  2);
        clockSourceDropdown.setSelectedId(proc.getMidiSyncEnabled() ? 2 : 1, false);
        clockSourceDropdown.onChange = [this](int id) { proc.setMidiSyncEnabled(id == 2); updateMidiSyncVisibility(); };
        addAndMakeVisible(clockSourceDropdown);

        makeFieldLabel(midiMessagesLabel, "Messages");
        midiMessagesDropdown.addItem("Clock only", 1);
        midiMessagesDropdown.addItem("Transport",  2);
        midiMessagesDropdown.addItem("Both",       3);
        midiMessagesDropdown.setSelectedId(proc.getMidiSyncMessages() + 1, false);
        midiMessagesDropdown.onChange = [this](int id) { proc.setMidiSyncMessages(id - 1); };
        addAndMakeVisible(midiMessagesDropdown);
        updateMidiSyncVisibility();

        midiClock.push_back({ "MIDI Clock", 2 * kRowH + kRowGap, [this](const Rows& r) {
            const int y2 = r.area.getY() + r.rowH + s(kRowGap);
            clockSourceLabel    .setBounds(r.labelX, r.area.getY(), r.labelW, r.rowH);
            clockSourceDropdown .setBounds(r.ctrlX,  r.area.getY(), r.ctrlW,  r.rowH);
            midiMessagesLabel   .setBounds(r.labelX, y2,            r.labelW, r.rowH);
            midiMessagesDropdown.setBounds(r.ctrlX,  y2,            r.ctrlW,  r.rowH); } });
    }
}

void StandardSettingsOverlay::makeFieldLabel(juce::Label& lbl, const juce::String& text)
{
    lbl.setText(text, juce::dontSendNotification);
    lbl.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(12.0f))));
    lbl.setColour(juce::Label::textColourId, MuLookAndFeel::colour(MuLookAndFeel::labelText));
    lbl.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(lbl);
}

void StandardSettingsOverlay::addProgramChangeSection(const juce::String& layerTableName,
                                                      const juce::String& fullTableName)
{
    midiPresetsBtn.setButtonText(layerTableName);
    fullPresetsBtn.setButtonText(fullTableName);
    midiPresetsBtn.onClick = [this] { if (onMidiPresetsClicked) onMidiPresetsClicked(); };
    fullPresetsBtn.onClick = [this] { if (onFullPresetsClicked) onFullPresetsClicked(); };
    addAndMakeVisible(midiPresetsBtn);
    addAndMakeVisible(fullPresetsBtn);
    addSection(Where::MidiAfterClock, { "MIDI Program Change", kRowH, [this](const Rows& r) {
        const int btnW = s(180), gap = s(MuLookAndFeel::kSpaceM);
        midiPresetsBtn.setBounds(r.labelX,              r.area.getY(), btnW, r.rowH);
        fullPresetsBtn.setBounds(r.labelX + btnW + gap, r.area.getY(), btnW, r.rowH); } });
}

void StandardSettingsOverlay::addSection(Where where, Section section, const juce::String& group)
{
    switch (where)
    {
        case Where::General:         general.push_back(std::move(section));    break;
        case Where::MidiBeforeClock: midiBefore.push_back(std::move(section)); break;
        case Where::MidiAfterClock:  midiAfter.push_back(std::move(section));  break;
        case Where::Group:           extraGroups.push_back({ group, std::move(section) }); break;
    }
}

void StandardSettingsOverlay::updateMidiSyncVisibility()
{
    // The Messages row only matters when MIDI-clock input is selected.
    const bool on = proc.getMidiSyncEnabled();
    midiMessagesLabel   .setVisible(on);
    midiMessagesDropdown.setVisible(on);
}

void StandardSettingsOverlay::layoutContent()
{
    headings.clear();
    placed.clear();

    const Rows base { {}, rowLabelX(), rowControlX(), s(kLabelW), s(kControlW), s(kRowH) };
    int  y = contentTop();
    bool firstGroup = true;

    // Stack one group: its heading (if named), then each section — heading + rows — with the
    // standard gaps between sections and between groups.
    auto emitGroup = [&](const juce::String& name, const std::vector<const Section*>& sections)
    {
        if (sections.empty()) return;
        if (! firstGroup) y += s(kGroupGap);
        firstGroup = false;
        if (name.isNotEmpty()) { headings.push_back({ y, name, true }); y += s(kGroupHeadH); }
        for (size_t i = 0; i < sections.size(); ++i)
        {
            const auto* sec = sections[i];
            if (i > 0) y += s(kSectionGap);
            if (sec->title.isNotEmpty()) { headings.push_back({ y, sec->title, false }); y += s(kSectionHeadH); }
            Rows rows = base;
            rows.area = { contentX(), y, contentW(), s(sec->heightPx) };
            if (sec->place) sec->place(rows);
            placed.push_back({ sec, rows });
            y += s(sec->heightPx);
        }
    };
    auto ptrs = [](const std::vector<Section>& v)
    {
        std::vector<const Section*> out;
        for (const auto& sct : v) out.push_back(&sct);
        return out;
    };

    emitGroup("General", ptrs(general));

    std::vector<const Section*> midi = ptrs(midiBefore);
    for (const auto* sct : ptrs(midiClock)) midi.push_back(sct);
    for (const auto* sct : ptrs(midiAfter)) midi.push_back(sct);
    emitGroup("MIDI", midi);

    // Extra groups, in the order each group name first appeared.
    juce::StringArray order;
    for (const auto& gs : extraGroups) if (! order.contains(gs.group)) order.add(gs.group);
    for (const auto& name : order)
    {
        std::vector<const Section*> secs;
        for (const auto& gs : extraGroups) if (gs.group == name) secs.push_back(&gs.section);
        emitGroup(name, secs);
    }
}

void StandardSettingsOverlay::paintContent(juce::Graphics& g)
{
    for (const auto& h : headings)
    {
        if (h.group) drawGroupHeader  (g, h.y, h.title);
        else         drawSectionHeader(g, h.y, h.title);
    }
    for (const auto& [sec, rows] : placed)
        if (sec->paint) sec->paint(g, rows);
}

} // namespace mu_ui
