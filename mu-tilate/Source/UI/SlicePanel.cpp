#include "UI/SlicePanel.h"

#include "Audio/AlgorithmNames.h"      // mu-core: the insert algorithm names
#include "Audio/InsertSlotConfig.h"    // mu-core: each algorithm's four knobs
#include "ValueFormat.h"               // mu-core: mu_fmt

#include <cmath>

namespace mu_tilate
{

namespace
{
    using LF = MuLookAndFeel;
    constexpr int kHeaderH = LF::kLoopRowH;   // the description line and the step buttons (unscaled)

    juce::String signedNumber(double v, int decimals, const juce::String& unit)
    {
        return (v > 0.0 ? "+" : "") + juce::String(v, decimals) + (unit.isEmpty() ? juce::String() : " " + unit);
    }
    juce::String signedDb(double v)      { return signedNumber(v, 1, "dB"); }
    juce::String signedMs(double v)      { return signedNumber(std::round(v), 0, "ms"); }
    juce::String octaves(double v)       { return signedNumber(v, 2, "oct"); }
    juce::String semitones(double v)     { return signedNumber(std::round(v), 0, "st"); }
    juce::String cents(double v)         { return signedNumber(std::round(v), 0, "cents"); }
    juce::String signedPercent(double v) { return signedNumber(std::round(v * 100.0), 0, "%"); }
    juce::String percent(double v)       { return juce::String((int) std::lround(v * 100.0)) + "%"; }
    juce::String panText(double v)
    {
        if (std::abs(v) < 0.005) return "No change";
        return (v < 0.0 ? "L " : "R ") + juce::String((int) std::lround(std::abs(v) * 100.0)) + "%";
    }
}

int SlicePanel::preferredHeight() noexcept
{
    // The header, the band, then the play strip (its name plate and one knob row in a box).
    return kHeaderH + LF::kSpaceS + VoiceBand::kHeight + LF::kSpaceS + LF::kSectionPlateH + LF::kKnobSize2H + LF::kSpaceS * 2;
}

SlicePanel::SlicePanel()
{
    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);
    prevButton.onClick = [this] { if (onStep) onStep(-1); };
    nextButton.onClick = [this] { if (onStep) onStep(+1); };

    // The layer's choices, shown where the Layer view has its dropdowns: greyed, as they are edited there.
    for (int i = 0; i < filterTypeNames().size(); ++i) layerFilterName.addItem(filterTypeNames()[i], i + 1);
    for (int i = 0; i < mu_ui::kInsertAlgoCount; ++i) layerInsertName.addItem(mu_audio::kInsertAlgorithmNames[i], i + 1);
    for (auto* d : { &layerFilterName, &layerInsertName }) d->setEnabled(false);
    layerFilterName.getComboBox().setTooltip("The layer's filter type (change it in the Layer view). The knobs here move its cutoff and resonance for this slice");
    layerInsertName.getComboBox().setTooltip("The layer's insert effect (change it in the Layer view). The knobs here move its four knobs for this slice");

    // Pitch: added to the layer's.
    addKnob(&pitchSection, 0, 0, "Pitch", &SliceSettings::pitchSemi, -12.0, 12.0, 1.0, LF::knobEuclidean, "Slice Pitch: semitones added to the layer's pitch", semitones);
    addKnob(&pitchSection, 0, 1, "Fine", &SliceSettings::fineCents, -50.0, 50.0, 1.0, LF::knobEuclidean, "Slice Fine Tune: cents added to the layer's", cents);

    // Filter: the layer chooses the type; the slice moves its cutoff and resonance and how far its envelope moves the cutoff.
    filterSection.place(layerFilterName, 0, 0, 2, true);
    addKnob(&filterSection, 0, 2, "Cutoff", &SliceSettings::cutoffOct, -4.0, 4.0, 0.01, LF::knobPostPad, "Slice Cutoff Offset: moves the layer's filter cutoff for this slice, in octaves", octaves);
    addKnob(&filterSection, 0, 3, "Res", &SliceSettings::resOffset, -1.0, 1.0, 0.01, LF::knobPostPad, "Slice Resonance Offset: added to the layer's filter resonance", signedPercent);
    addKnob(&filterSection, 0, 4, "Depth", &SliceSettings::filterEnvAmt, -1.0, 1.0, 0.01, LF::knobPostPad, "Slice Filter Envelope Depth: added to how far the layer's filter envelope moves the cutoff", signedPercent);
    addKnob(&filterSection, 1, 0, "A", &SliceSettings::fltA, -1000.0, 1000.0, 1.0, LF::knobPostPad, "Filter Attack Offset: added to the layer's filter attack", signedMs);
    addKnob(&filterSection, 1, 1, "D", &SliceSettings::fltD, -1000.0, 1000.0, 1.0, LF::knobPostPad, "Filter Decay Offset: added to the layer's filter decay", signedMs);
    addKnob(&filterSection, 1, 2, "S", &SliceSettings::fltS, -1.0, 1.0, 0.01, LF::knobPostPad, "Filter Sustain Offset: added to the layer's filter sustain", signedPercent);
    addKnob(&filterSection, 1, 3, "R", &SliceSettings::fltR, -1000.0, 1000.0, 1.0, LF::knobPostPad, "Filter Release Offset: added to the layer's filter release", signedMs);

    // Amp: level and pan, the fades, and the amp envelope.
    auto fadeText = [](double v) { return signedNumber(v, 1, "ms"); };
    addKnob(&ampSection, 0, 0, "Level", &SliceSettings::levelDb, -12.0, 12.0, 0.1, LF::knobLevel, "Slice Level: dB added to the layer's level", signedDb);
    addKnob(&ampSection, 0, 1, "Pan", &SliceSettings::pan, -1.0, 1.0, 0.01, LF::knobPan, "Slice Pan: moves this slice from where the layer sits in the stereo field", panText);
    addKnob(&ampSection, 0, 2, "In", &SliceSettings::fadeInMs, -25.0, 25.0, 0.1, LF::knobModulation, "Fade In Offset: added to the layer's fade in", fadeText);
    addKnob(&ampSection, 0, 3, "Out", &SliceSettings::fadeOutMs, -25.0, 25.0, 0.1, LF::knobModulation, "Fade Out Offset: added to the layer's fade out", fadeText);
    addKnob(&ampSection, 1, 0, "A", &SliceSettings::ampA, -1000.0, 1000.0, 1.0, LF::knobLevel, "Amp Attack Offset: added to the layer's amp attack", signedMs);
    addKnob(&ampSection, 1, 1, "D", &SliceSettings::ampD, -1000.0, 1000.0, 1.0, LF::knobLevel, "Amp Decay Offset: added to the layer's amp decay", signedMs);
    addKnob(&ampSection, 1, 2, "S", &SliceSettings::ampS, -1.0, 1.0, 0.01, LF::knobLevel, "Amp Sustain Offset: added to the layer's amp sustain", signedPercent);
    addKnob(&ampSection, 1, 3, "R", &SliceSettings::ampR, -1000.0, 1000.0, 1.0, LF::knobLevel, "Amp Release Offset: added to the layer's amp release", signedMs);

    // Effects: the layer's insert, and an offset for each of its (up to four) knobs.
    insertSection.place(layerInsertName, 0, 0, 4, true);
    for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
    {
        auto k = std::make_unique<Knob>();
        k->comp = std::make_unique<KnobWithLabel>("P" + juce::String(slot + 1), LF::knobInsertPad);
        k->insSlot = slot;
        k->comp->setRange(-1.0, 1.0, 0.01);
        k->comp->statusTextFromValue = [this, slot](double v) { return insertOffsetText(slot, v); };
        k->comp->onStatusUpdate = [this, slot](const juce::String&, const juce::String& v)
        {
            if (! onStatusUpdate) return;
            const auto& cfg = mu_ui::kInsertAlgoSlots[juce::jlimit(0, mu_ui::kInsertAlgoCount - 1, insertAlgo)][slot];
            onStatusUpdate(juce::String("Slice ") + (cfg.label != nullptr ? cfg.label : "Insert") + " Offset: added to the layer's insert knob", v);
        };
        k->comp->onValueChanged = [this](double) { changed(); };
        insertSection.place(*k->comp, 1, slot);
        knobs.push_back(std::move(k));
    }

    band.setTitlePrefix("SLICE");
    band.setSections(pitchSection, filterSection, ampSection, insertSection, { nullptr, nullptr, nullptr });
    addAndMakeVisible(band);

    // How the slice plays: direction, and the pre-fade (a quiet reversed copy of its start that swells into the hit).
    addSwitch(reverseSwitch);
    addSwitch(preSwitch);
    addKnob(nullptr, 0, 0, "Level", &SliceSettings::preLevelDb, -36.0, 0.0, 0.1, LF::knobFxSend, "Pre-fade Level: how loud the reversed swell is, below the slice",
            [](double v) { return juce::String(v, 1) + " dB"; });
    addKnob(nullptr, 0, 0, "Length", &SliceSettings::preLengthMs, 10.0, 1000.0, 1.0, LF::knobFxSend, "Pre-fade Length: how long the swell runs before the hit",
            [](double v) { return mu_fmt::unitText(mu_fmt::Unit::Ms, v); });
    addKnob(nullptr, 0, 0, "Tone", &SliceSettings::preTone, -1.0, 1.0, 0.01, LF::knobFxSend, "Pre-fade Tone: darker to the left, thinner to the right",
            [](double v) { return std::abs(v) < 0.005 ? juce::String("Neutral") : (v < 0.0 ? "Dark " : "Thin ") + percent(std::abs(v)); });
    for (size_t i = knobs.size() - 3; i < knobs.size(); ++i) playKnobs.push_back(knobs[i].get());
    playKnobs[0]->comp->getSlider().setSkewFactorFromMidPoint(-12.0);   // level: finer near the loud end
    playKnobs[1]->comp->getSlider().setSkewFactorFromMidPoint(150.0);   // length: finer for short swells

    setLayerInsertAlgorithm(0);
    setLayerFilterType(0);
}

// A knob of the band's section (or, with no section, of the play strip) at `row`, `col`, editing `field`.
void SlicePanel::addKnob(VoiceBandSection* section, int row, int col, const char* label, float SliceSettings::* field, double lo, double hi,
                         double step, MuLookAndFeel::ColourIds colour, const juce::String& statusName, std::function<juce::String(double)> format)
{
    auto k = std::make_unique<Knob>();
    k->comp = std::make_unique<KnobWithLabel>(label, colour);
    k->field = field;
    k->comp->setRange(lo, hi, step);
    k->comp->statusTextFromValue = std::move(format);
    k->comp->onStatusUpdate = [this, statusName](const juce::String&, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(statusName, v); };
    k->comp->onValueChanged = [this](double) { changed(); };
    if (section != nullptr) section->place(*k->comp, row, col);
    else                    addAndMakeVisible(*k->comp);
    knobs.push_back(std::move(k));
}

void SlicePanel::addSwitch(SegmentControl& control)
{
    addAndMakeVisible(control);
    control.onChange = [this](int) { changed(); };
}

// An insert knob's offset in the effect's own units where it has a plain linear range (dB, %), else as a share of the knob's travel.
juce::String SlicePanel::insertOffsetText(int slot, double v) const
{
    const auto& cfg = mu_ui::kInsertAlgoSlots[juce::jlimit(0, mu_ui::kInsertAlgoCount - 1, insertAlgo)][slot];
    if (cfg.label != nullptr && cfg.skew == mu_ui::SkewMode::Linear && cfg.maxVal > cfg.minVal)
        return signedNumber(v * (double) (cfg.maxVal - cfg.minVal), 1, cfg.unitSuffix);
    return signedPercent(v) + " of travel";
}

void SlicePanel::setLayerFilterType(int choice)
{
    layerFilterName.getComboBox().setSelectedId(juce::jlimit(0, filterTypeNames().size() - 1, choice) + 1, juce::dontSendNotification);
}

// Labels the four insert offsets from the algorithm's own slot table and hides the ones it does not use.
void SlicePanel::setLayerInsertAlgorithm(int algo)
{
    insertAlgo = juce::jlimit(0, mu_ui::kInsertAlgoCount - 1, algo);
    layerInsertName.getComboBox().setSelectedId(insertAlgo + 1, juce::dontSendNotification);
    for (auto& k : knobs)
    {
        if (k->insSlot < 0) continue;
        const auto& cfg = mu_ui::kInsertAlgoSlots[insertAlgo][k->insSlot];
        k->comp->setVisible(cfg.label != nullptr);
        if (cfg.label != nullptr) k->comp->setLabel(cfg.label);
        k->comp->getSlider().updateText();
    }
    repaint();
}

// Reads every control into the settings and reports them as one change.
void SlicePanel::changed()
{
    if (updating || slice < 0) return;
    for (auto& k : knobs)
    {
        if (k->field != nullptr) settings.*(k->field) = (float) k->comp->getValue();
        else if (k->insSlot >= 0) settings.insOff[k->insSlot] = (float) k->comp->getValue();
    }
    settings.reverse    = reverseSwitch.getSelectedIndex() == 1;
    settings.preFade    = preSwitch.getSelectedIndex() == 1;
    if (onChange) onChange(settings);
}

void SlicePanel::setSlice(int index, const SliceSettings& s, const juce::String& text)
{
    updating = true;
    slice = index;
    settings = s;
    description = text;
    for (auto& k : knobs)
    {
        if (k->field != nullptr) k->comp->setValue((double) (s.*(k->field)));
        else if (k->insSlot >= 0) k->comp->setValue((double) s.insOff[k->insSlot]);
    }
    reverseSwitch.setSelectedIndex(s.reverse ? 1 : 0);
    preSwitch.setSelectedIndex(s.preFade ? 1 : 0);
    updating = false;
    setEnabled(index >= 0);
    repaint();
}

void SlicePanel::paint(juce::Graphics& g)
{
    using mu_ui::s;
    g.setFont(juce::Font(juce::FontOptions {}.withHeight(mu_ui::sf(11.0f))));
    g.setColour(LF::colour(LF::labelText));
    g.drawText(slice >= 0 ? description : juce::String("Select a hit on the timeline, or a slice in the waveform (or use < >)"),
               s(60), 0, getWidth() - s(60), s(kHeaderH), juce::Justification::centredLeft);
    LF::drawSections(g, *this, sections, LF::appAccent(*this));
}

void SlicePanel::resized()
{
    using mu_ui::s;
    auto area = getLocalBounds();
    auto header = area.removeFromTop(s(kHeaderH));
    prevButton.setBounds(header.removeFromLeft(s(LF::kLoopStepButtonW)));
    header.removeFromLeft(s(4));
    nextButton.setBounds(header.removeFromLeft(s(LF::kLoopStepButtonW)));
    area.removeFromTop(s(LF::kSpaceS));

    // The band: the same Pitch | Filter | Amp | Effects boxes the Layer view has.
    band.setBounds(area.removeFromTop(s(VoiceBand::kHeight)).withWidth(s(VoiceBand::kEffectsX + VoiceBand::kEffectsW)));
    area.removeFromTop(s(LF::kSpaceS));

    // Under the band, two raised boxes side by side: the slice's direction, and the pre-fade with its switch and three knobs.
    sections.clear();
    const int pad = s(LF::kSpaceS), plate = s(LF::kSectionPlateH);
    const int boxTop = area.getY() + plate, top = boxTop + pad;
    int x = s(VoiceBand::kLeftGap);
    auto switchAt = [&](SegmentControl& sw, int w)
    {
        sw.setBounds(x, top + (s(LF::kKnobSize2H) - s(kHeaderH)) / 2, s(w), s(kHeaderH));
        x += s(w) + pad;
    };
    auto closeBox = [&](int start, const char* title)
    {
        sections.push_back({ juce::Rectangle<int>(start, boxTop, x - start, s(LF::kKnobSize2H) + pad * 2), title });
        x += pad;
    };

    int start = x;
    x += pad;
    switchAt(reverseSwitch, LF::kLoopSwitchW);
    closeBox(start, "SLICE PLAY");

    start = x;
    x += pad;
    switchAt(preSwitch, LF::kLoopSwitchW);
    for (auto* k : playKnobs) { k->comp->setBounds(x, top, s(LF::kKnobSize2W), s(LF::kKnobSize2H)); x += s(LF::kKnobSize2W); }
    x += pad;
    closeBox(start, "PRE-FADE");
}

} // namespace mu_tilate
