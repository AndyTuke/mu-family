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
    addKnob(&pitchSection, 0, 0, "Pitch", &SliceSettings::pitchSemi, -12.0, 12.0, 1.0, LF::knobEuclidean, "Slice Pitch: semitones added to the layer's pitch", semitones, (int) OverrideField::PitchSemi);
    addKnob(&pitchSection, 0, 1, "Fine", &SliceSettings::fineCents, -50.0, 50.0, 1.0, LF::knobEuclidean, "Slice Fine Tune: cents added to the layer's", cents, (int) OverrideField::FineCents);

    // Filter: the layer chooses the type; the slice moves its cutoff and resonance and how far its envelope moves the cutoff.
    filterSection.place(layerFilterName, 0, 0, 2, true);
    addKnob(&filterSection, 0, 2, "Cutoff", &SliceSettings::cutoffOct, -4.0, 4.0, 0.01, LF::knobPostPad, "Slice Cutoff Offset: moves the layer's filter cutoff for this slice, in octaves", octaves, (int) OverrideField::CutoffOct);
    addKnob(&filterSection, 0, 3, "Res", &SliceSettings::resOffset, -1.0, 1.0, 0.01, LF::knobPostPad, "Slice Resonance Offset: added to the layer's filter resonance", signedPercent, (int) OverrideField::ResOffset);
    addKnob(&filterSection, 0, 4, "Depth", &SliceSettings::filterEnvAmt, -1.0, 1.0, 0.01, LF::knobPostPad, "Slice Filter Envelope Depth: added to how far the layer's filter envelope moves the cutoff", signedPercent, (int) OverrideField::FilterEnvAmt);
    addKnob(&filterSection, 1, 0, "A", &SliceSettings::fltA, -1000.0, 1000.0, 1.0, LF::knobPostPad, "Filter Attack Offset: added to the layer's filter attack", signedMs, (int) OverrideField::FltA);
    addKnob(&filterSection, 1, 1, "D", &SliceSettings::fltD, -1000.0, 1000.0, 1.0, LF::knobPostPad, "Filter Decay Offset: added to the layer's filter decay", signedMs, (int) OverrideField::FltD);
    addKnob(&filterSection, 1, 2, "S", &SliceSettings::fltS, -1.0, 1.0, 0.01, LF::knobPostPad, "Filter Sustain Offset: added to the layer's filter sustain", signedPercent, (int) OverrideField::FltS);
    addKnob(&filterSection, 1, 3, "R", &SliceSettings::fltR, -1000.0, 1000.0, 1.0, LF::knobPostPad, "Filter Release Offset: added to the layer's filter release", signedMs, (int) OverrideField::FltR);

    // Amp: level and pan, the fades, and the amp envelope.
    auto fadeText = [](double v) { return signedNumber(v, 1, "ms"); };
    addKnob(&ampSection, 0, 0, "Level", &SliceSettings::levelDb, -12.0, 12.0, 0.1, LF::knobLevel, "Slice Level: dB added to the layer's level", signedDb, (int) OverrideField::LevelDb);
    addKnob(&ampSection, 0, 1, "Pan", &SliceSettings::pan, -1.0, 1.0, 0.01, LF::knobPan, "Slice Pan: moves this slice from where the layer sits in the stereo field", panText, (int) OverrideField::Pan);
    addKnob(&ampSection, 0, 2, "In", &SliceSettings::fadeInMs, -25.0, 25.0, 0.1, LF::knobModulation, "Fade In Offset: added to the layer's fade in", fadeText, (int) OverrideField::FadeIn);
    addKnob(&ampSection, 0, 3, "Out", &SliceSettings::fadeOutMs, -25.0, 25.0, 0.1, LF::knobModulation, "Fade Out Offset: added to the layer's fade out", fadeText, (int) OverrideField::FadeOut);
    addKnob(&ampSection, 1, 0, "A", &SliceSettings::ampA, -1000.0, 1000.0, 1.0, LF::knobLevel, "Amp Attack Offset: added to the layer's amp attack", signedMs, (int) OverrideField::AmpA);
    addKnob(&ampSection, 1, 1, "D", &SliceSettings::ampD, -1000.0, 1000.0, 1.0, LF::knobLevel, "Amp Decay Offset: added to the layer's amp decay", signedMs, (int) OverrideField::AmpD);
    addKnob(&ampSection, 1, 2, "S", &SliceSettings::ampS, -1.0, 1.0, 0.01, LF::knobLevel, "Amp Sustain Offset: added to the layer's amp sustain", signedPercent, (int) OverrideField::AmpS);
    addKnob(&ampSection, 1, 3, "R", &SliceSettings::ampR, -1000.0, 1000.0, 1.0, LF::knobLevel, "Amp Release Offset: added to the layer's amp release", signedMs, (int) OverrideField::AmpR);

    // Effects: the layer's insert, and an offset for each of its (up to four) knobs.
    insertSection.place(layerInsertName, 0, 0, 4, true);
    for (int slot = 0; slot < mu_ui::kInsertSlotCount; ++slot)
    {
        auto k = std::make_unique<Knob>();
        k->comp = std::make_unique<KnobWithLabel>("P" + juce::String(slot + 1), LF::knobInsertPad);
        k->insSlot = slot;
        k->ovField = (int) OverrideField::Ins0 + slot;
        k->offLo = -1.0; k->offHi = 1.0; k->offStep = 0.01;
        k->comp->setRange(-1.0, 1.0, 0.01);
        k->offFormat = [this, slot](double v) { return insertOffsetText(slot, v); };
        k->comp->onValueChanged = [this](double) { changed(); };
        setupOverride(*k);
        insertSection.place(*k->comp, 1, slot);
        knobs.push_back(std::move(k));
    }

    // The three FX sends: how far this slice moves each of the strip's sends (the mixer's own knobs stay where they are). They sit in
    // their own box under the band, not among the layer's send knobs, so they are not mistaken for them.
    static const char* const sendNames[3]  = { "Effect", "Delay", "Reverb" };
    for (int i = 0; i < 3; ++i)
    {
        auto k = std::make_unique<Knob>();
        k->comp = std::make_unique<KnobWithLabel>(sendNames[i], LF::knobFxSend);
        k->sendSlot = i;
        k->ovField = (int) OverrideField::Send0 + i;
        k->offLo = -1.0; k->offHi = 1.0; k->offStep = 0.01;
        k->comp->setRange(-1.0, 1.0, 0.01);
        k->offFormat = signedPercent;
        k->statusBase = juce::String("Slice ") + sendNames[i] + " Send";
        k->comp->onValueChanged = [this](double) { changed(); };
        setupOverride(*k);
        sendKnobs[(size_t) i] = k->comp.get();
        addAndMakeVisible(*k->comp);
        knobs.push_back(std::move(k));
    }

    band.setTitlePrefix("SLICE");
    band.setSections(pitchSection, filterSection, ampSection, insertSection, { nullptr, nullptr, nullptr });
    addAndMakeVisible(band);

    // How the slice plays: direction, and the pre-fade (a quiet reversed copy of its start that swells into the hit).
    mu_ui::setStatusName(reverseSwitch, "Slice Direction");
    mu_ui::setStatusName(preSwitch, "Slice Pre-Fade");
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
                         double step, MuLookAndFeel::ColourIds colour, const juce::String& statusName, std::function<juce::String(double)> format,
                         int ovField)
{
    auto k = std::make_unique<Knob>();
    k->comp = std::make_unique<KnobWithLabel>(label, colour);
    k->field = ovField >= 0 ? nullptr : field;
    k->ovField = ovField;
    k->offLo = lo; k->offHi = hi; k->offStep = step;
    k->comp->setRange(lo, hi, step);
    k->offFormat = std::move(format);
    k->statusBase = statusName.upToFirstOccurrenceOf(":", false, false).trim();   // "Slice Pitch: semitones added ..." -> "Slice Pitch"
    if (k->statusBase.endsWith(" Offset")) k->statusBase = k->statusBase.dropLastCharacters(7);   // the mode is added when it is shown
    if (ovField >= 0)
        setupOverride(*k);
    else   // the pre-fade's own knobs: absolute by nature, no menu
    {
        k->comp->statusTextFromValue = k->offFormat;
        k->comp->onStatusUpdate = [this, statusName](const juce::String&, const juce::String& v) { if (onStatusUpdate) onStatusUpdate(statusName, v); };
    }
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

// The absolute range, skew and text of each overridable knob, in the control's own unit (the layer's own ranges).
namespace
{
    struct AbsMeta { double lo, hi, step, skewMid; };
    AbsMeta absMeta(OverrideField f) noexcept
    {
        switch (f)
        {
            case OverrideField::PitchSemi:    return { -24.0, 24.0, 1.0, 0.0 };
            case OverrideField::FineCents:    return { -100.0, 100.0, 1.0, 0.0 };
            case OverrideField::CutoffOct:    return { 20.0, 20000.0, 0.0, 1000.0 };
            case OverrideField::ResOffset:    return { 0.0, 1.0, 0.01, 0.0 };
            case OverrideField::FilterEnvAmt: return { -1.0, 1.0, 0.01, 0.0 };
            case OverrideField::FltA: case OverrideField::FltD: case OverrideField::FltR:
            case OverrideField::AmpA: case OverrideField::AmpD: case OverrideField::AmpR: return { 0.0, 2000.0, 1.0, 100.0 };
            case OverrideField::FltS: case OverrideField::AmpS: return { 0.0, 1.0, 0.01, 0.0 };
            case OverrideField::LevelDb:      return { -60.0, 6.0, 0.1, 0.0 };
            case OverrideField::Pan:          return { -1.0, 1.0, 0.01, 0.0 };
            case OverrideField::FadeIn: case OverrideField::FadeOut: return { 0.0, 50.0, 0.1, 5.0 };
            default:                          return { 0.0, 1.0, 0.01, 0.0 };   // the insert knobs and the sends
        }
    }
    juce::String plainNumber(double v, int decimals, const juce::String& unit) { return juce::String(v, decimals) + " " + unit; }
}

// The knob's value as text when it is Absolute: the plain value in the control's own unit, no sign ("7 st", "800 ms", "-6.0 dB").
juce::String SlicePanel::absoluteText(const Knob& k, double v) const
{
    switch ((OverrideField) k.ovField)
    {
        case OverrideField::PitchSemi:    return plainNumber(std::round(v), 0, "st");
        case OverrideField::FineCents:    return plainNumber(std::round(v), 0, "cents");
        case OverrideField::CutoffOct:    return mu_fmt::unitText(mu_fmt::Unit::Hz, v);
        case OverrideField::ResOffset: case OverrideField::FilterEnvAmt: case OverrideField::FltS: case OverrideField::AmpS: return percent(v);
        case OverrideField::FltA: case OverrideField::FltD: case OverrideField::FltR:
        case OverrideField::AmpA: case OverrideField::AmpD: case OverrideField::AmpR: return plainNumber(std::round(v), 0, "ms");
        case OverrideField::LevelDb:      return plainNumber(v, 1, "dB");
        case OverrideField::Pan:          return std::abs(v) < 0.005 ? juce::String("Centre") : (v < 0.0 ? "L " : "R ") + juce::String((int) std::lround(std::abs(v) * 100.0)) + "%";
        case OverrideField::FadeIn: case OverrideField::FadeOut: return plainNumber(v, 1, "ms");
        case OverrideField::Ins0: case OverrideField::Ins1: case OverrideField::Ins2: case OverrideField::Ins3:
        {
            const auto& cfg = mu_ui::kInsertAlgoSlots[juce::jlimit(0, mu_ui::kInsertAlgoCount - 1, insertAlgo)][k.insSlot];
            if (cfg.label != nullptr && cfg.skew == mu_ui::SkewMode::Linear && cfg.maxVal > cfg.minVal)
                return plainNumber((double) cfg.minVal + v * (double) (cfg.maxVal - cfg.minVal), 1, cfg.unitSuffix);
            return percent(v) + " of travel";
        }
        default:                          return percent(v);   // the sends
    }
}

// What the knob itself shows while Absolute: the actual value as a plain number in the control's own unit (the unit is on the status bar).
// Fractions are shown as percent, and an insert knob with a linear range in the effect's own units.
juce::String SlicePanel::readoutText(const Knob& k, double v) const
{
    switch ((OverrideField) k.ovField)
    {
        case OverrideField::PitchSemi: case OverrideField::FineCents: return juce::String((int) std::lround(v));
        case OverrideField::CutoffOct:    return v >= 1000.0 ? juce::String(v / 1000.0, 1) + "k" : juce::String((int) std::lround(v));
        case OverrideField::FltA: case OverrideField::FltD: case OverrideField::FltR:
        case OverrideField::AmpA: case OverrideField::AmpD: case OverrideField::AmpR: return juce::String((int) std::lround(v));
        case OverrideField::LevelDb: case OverrideField::FadeIn: case OverrideField::FadeOut: return juce::String(v, 1);
        case OverrideField::Ins0: case OverrideField::Ins1: case OverrideField::Ins2: case OverrideField::Ins3:
        {
            const auto& cfg = mu_ui::kInsertAlgoSlots[juce::jlimit(0, mu_ui::kInsertAlgoCount - 1, insertAlgo)][k.insSlot];
            if (cfg.label != nullptr && cfg.skew == mu_ui::SkewMode::Linear && cfg.maxVal > cfg.minVal)
                return juce::String((double) cfg.minVal + v * (double) (cfg.maxVal - cfg.minVal), 1);
            return juce::String((int) std::lround(v * 100.0));
        }
        default:                          return juce::String((int) std::lround(v * 100.0));   // resonance, depth, sustains, pan, sends: percent
    }
}

double SlicePanel::readoutValue(const Knob& k, const juce::String& text) const
{
    const double x = text.getDoubleValue() * (text.trimEnd().endsWithIgnoreCase("k") ? 1000.0 : 1.0);
    switch ((OverrideField) k.ovField)
    {
        case OverrideField::PitchSemi: case OverrideField::FineCents: case OverrideField::CutoffOct:
        case OverrideField::FltA: case OverrideField::FltD: case OverrideField::FltR:
        case OverrideField::AmpA: case OverrideField::AmpD: case OverrideField::AmpR:
        case OverrideField::LevelDb: case OverrideField::FadeIn: case OverrideField::FadeOut: return x;
        case OverrideField::Ins0: case OverrideField::Ins1: case OverrideField::Ins2: case OverrideField::Ins3:
        {
            const auto& cfg = mu_ui::kInsertAlgoSlots[juce::jlimit(0, mu_ui::kInsertAlgoCount - 1, insertAlgo)][k.insSlot];
            if (cfg.label != nullptr && cfg.skew == mu_ui::SkewMode::Linear && cfg.maxVal > cfg.minVal)
                return (x - (double) cfg.minVal) / (double) (cfg.maxVal - cfg.minVal);
            return x / 100.0;
        }
        default:                          return x / 100.0;
    }
}

// An Absolute knob reads as its value. An Offset knob reads as the offset and, after an equals sign, what it comes to on the layer's
// value ("+3 st = 8 st"), taken from the knob's live value so it follows a drag.
juce::String SlicePanel::knobStatusText(const Knob& k, double v) const
{
    if (k.ovField < 0) return k.offFormat(v);
    if (settings.ov[k.ovField].absolute) return absoluteText(k, v);
    if (! layerValues) return k.offFormat(v);
    const auto f = (OverrideField) k.ovField;
    const double base = (double) layerValueOf(layerValues(), f);
    const auto m = absMeta(f);
    const double total = juce::jlimit(m.lo, m.hi, f == OverrideField::CutoffOct ? base * std::exp2(v) : base + v);
    return k.offFormat(v) + " = " + absoluteText(k, total);
}

// What the status bar shows for a knob: its name with the mode, and its value in that mode.
void SlicePanel::reportKnob(const Knob& k)
{
    if (! onStatusUpdate) return;
    juce::String base = k.statusBase;
    if (k.insSlot >= 0)
    {
        const auto& cfg = mu_ui::kInsertAlgoSlots[juce::jlimit(0, mu_ui::kInsertAlgoCount - 1, insertAlgo)][k.insSlot];
        base = juce::String("Slice ") + (cfg.label != nullptr ? cfg.label : "Insert");
    }
    onStatusUpdate(base + (settings.ov[k.ovField].absolute ? " (Absolute)" : " (Offset)"), knobStatusText(k, k.comp->getValue()));
}

// The right-click menu, the one hint and the status reporting of an overridable knob.
void SlicePanel::setupOverride(Knob& k)
{
    k.comp->statusTextFromValue = [this, kp = &k](double v) { return knobStatusText(*kp, v); };
    k.comp->onStatusUpdate = [this, kp = &k](const juce::String&, const juce::String&) { reportKnob(*kp); };
    k.comp->getSlider().setTooltip("Right-click: Offset, Absolute or Clear");
    k.comp->contextMenuProvider = [this, kp = &k]() -> std::vector<KnobWithLabel::MenuEntry>
    {
        if (slice < 0) return {};
        const bool abs = settings.ov[kp->ovField].absolute;
        return { { "Offset (adds to the layer)", [this, kp] { setKnobMode(*kp, Mode::offset); },   ! abs, false },
                 { "Absolute (sets the value)",  [this, kp] { setKnobMode(*kp, Mode::absolute); }, abs,   false },
                 { "Clear (back to the layer)",  [this, kp] { setKnobMode(*kp, Mode::clear); },    false, true } };
    };
}

// Puts the knob into the mode its settings say: the range, text and ring of that mode, and the value it holds.
void SlicePanel::applyKnob(Knob& k)
{
    const auto f = (OverrideField) k.ovField;
    const bool abs = settings.ov[k.ovField].absolute;
    auto& c = *k.comp;
    const bool was = updating;
    updating = true;
    if (abs)
    {
        const auto m = absMeta(f);
        c.setRange(m.lo, m.hi, m.step);
        if (m.skewMid > 0.0) c.getSlider().setSkewFactorFromMidPoint(m.skewMid); else c.getSlider().setSkewFactor(1.0);
        // The knob's own number is the actual value, so the knob always shows what the slice plays.
        c.getSlider().textFromValueFunction = [this, kp = &k](double v) { return readoutText(*kp, v); };
        c.getSlider().valueFromTextFunction = [this, kp = &k](const juce::String& t) { return readoutValue(*kp, t); };
        c.setValue((double) settings.ov[k.ovField].absValue);
        c.setOverrideMode(KnobWithLabel::OverrideMode::absolute);
    }
    else
    {
        c.setRange(k.offLo, k.offHi, k.offStep);
        c.getSlider().setSkewFactor(1.0);
        c.getSlider().textFromValueFunction = nullptr;   // back to the slider's own number
        c.getSlider().valueFromTextFunction = nullptr;
        c.setValue((double) offsetOf(settings, f));
        c.setOverrideMode(KnobWithLabel::OverrideMode::offset);
    }
    c.getSlider().updateText();
    updating = was;
    c.repaint();
}

// The menu's actions. Absolute starts at what the knob sounds like now (the layer's value moved by the current offset), so choosing it
// changes nothing you hear; Offset restores the offset the knob had; Clear puts the knob back as it was before any change.
void SlicePanel::setKnobMode(Knob& k, Mode mode)
{
    if (slice < 0) return;
    auto& o = settings.ov[k.ovField];
    const auto f = (OverrideField) k.ovField;
    if (mode == Mode::absolute)
    {
        if (o.absolute) return;
        const LayerValues lv = layerValues ? layerValues() : LayerValues{};
        const auto m = absMeta(f);
        o.absValue = (float) juce::jlimit(m.lo, m.hi, (double) effectiveValue(settings, lv, f));   // before the flag flips: it reads the offset
        o.absolute = true;
    }
    else if (mode == Mode::offset)
    {
        if (! o.absolute) return;
        o.absolute = false;
    }
    else
    {
        o.absolute = false;
        o.absValue = 0.0f;
        offsetRef(settings, f) = 0.0f;
    }
    applyKnob(k);
    if (onChange) onChange(settings);
    if (mode == Mode::clear)
    {
        if (onStatusUpdate) onStatusUpdate(k.statusBase.isEmpty() ? juce::String("Slice knob") : k.statusBase, "Cleared");
    }
    else reportKnob(k);
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
        if (k->ovField >= 0)
        {
            // The knob is the offset, or (Absolute) the actual value; the other one is left as it is.
            const auto f = (OverrideField) k->ovField;
            if (settings.ov[k->ovField].absolute) settings.ov[k->ovField].absValue = (float) k->comp->getValue();
            else                                  offsetRef(settings, f) = (float) k->comp->getValue();
        }
        else if (k->field != nullptr) settings.*(k->field) = (float) k->comp->getValue();
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
        if (k->ovField >= 0) applyKnob(*k);
        else if (k->field != nullptr) k->comp->setValue((double) (s.*(k->field)));
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
    g.setFont(MuLookAndFeel::uiFont(mu_ui::sf(11.0f)));
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

    start = x;
    x += pad;
    for (auto* k : sendKnobs) { k->setBounds(x, top, s(LF::kKnobSize2W), s(LF::kKnobSize2H)); x += s(LF::kKnobSize2W); }
    x += pad;
    closeBox(start, "SLICE SENDS");
}

} // namespace mu_tilate
