#include "FilterSubsection.h"
#include "Plugin/PluginProcessor.h"
#include "Modulation/ModulationSnapshot.h"
#include "Sequencer/Rhythm.h"

namespace {
static juce::String cutoffLabelStr(double hz)
{
    return hz < 1000.0 ? "Cutoff (Hz)" : "Cutoff (kHz)";
}
static juce::String adsrLabelStr(const juce::String& name, double v)
{
    return name + (v < 1.0 ? " (ms)" : " (s)");
}
static juce::String adsrValueStr(double v)
{
    double ms = std::max(1.0, v * 1000.0);
    return (ms < 1000.0) ? juce::String((int)std::round(ms))
                         : juce::String(ms / 1000.0, 2);
}
static juce::String formatAdsrTimeSec(double v)
{
    double ms = std::max(1.0, v * 1000.0);
    if (ms < 1000.0)
        return juce::String((int)std::round(ms)) + " ms";
    return juce::String(ms / 1000.0, 2) + " s";
}
static double parseAdsrTimeSec(const juce::String& s)
{
    auto t = s.trim().toLowerCase();
    if (t.endsWith("ms"))
        return t.dropLastCharacters(2).trim().getDoubleValue() / 1000.0;
    if (t.endsWith("s"))
        return t.dropLastCharacters(1).trim().getDoubleValue();
    return t.getDoubleValue() / 1000.0;
}
} // namespace

FilterSubsection::FilterSubsection(PluginProcessor& p) : proc(p)
{
    // Types 0-3,9: SVF. Types 4-6,10: LadderFilter. 7: 1-pole LP. 11: 1-pole HP. 8: Comb. 12-14: Biquad EQ.
    filterType.addItem("LP 6",    8);   filterType.addItem("LP 12",   1);
    filterType.addItem("LP 24",   5);   filterType.addItem("BP 12",   3);
    filterType.addItem("BP 24",   7);   filterType.addItem("HP 6",    12);
    filterType.addItem("HP 12",   2);   filterType.addItem("HP 24",   6);
    filterType.addItem("Notch",   4);   filterType.addItem("Notch 24",11);
    filterType.addItem("AP 12",   10);  filterType.addItem("Comb +",  9);
    filterType.addItem("Comb -",  16);  filterType.addItem("Peak",    13);
    filterType.addItem("Lo Shf",  14);  filterType.addItem("Hi Shf",  15);
    filterType.setSelectedId(1, false);
    addAndMakeVisible(filterType);

    for (auto* k : { &filterCutoff, &filterRes,
                     &filterAtk, &filterDec, &filterSus, &filterRel, &filterDepth,
                     &filterLowCut })
        addAndMakeVisible(k);
    addAndMakeVisible(filterLegCtrl);

    filterCutoff.setRange(20.0, 20000.0, 1.0);  filterCutoff.setValue(8000.0);
    filterCutoff.getSlider().setSkewFactorFromMidPoint(640.0);
    filterRes   .setRange(0.0,  100.0,  0.1);   filterRes   .setValue(20.0);
    filterAtk   .setRange(0.0,  10.0, 0.001);  filterAtk.setValue(0.03);  filterAtk.getSlider().setSkewFactor(0.3);
    filterDec   .setRange(0.0,  10.0, 0.001);  filterDec.setValue(0.09);  filterDec.getSlider().setSkewFactor(0.3);
    filterSus   .setRange(0.0, 100.0, 0.1);    filterSus.setValue(0.0);
    filterRel   .setRange(0.0,  10.0, 0.001);  filterRel.setValue(0.09);  filterRel.getSlider().setSkewFactor(0.3);
    filterDepth .setRange(0.0,  100.0,  1.0);  filterDepth.setValue(0.0);
    // Low cut: 0 (off) → 1 kHz, log-skewed so the audible low-end region gets
    // most of the knob travel.
    filterLowCut.setRange(0.0, 1000.0, 1.0);   filterLowCut.setValue(0.0);
    filterLowCut.getSlider().setSkewFactor(0.35);

    wireCallbacks();
}

void FilterSubsection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    auto it = paramPtrCache.find(suffix);
    if (it == paramPtrCache.end())
    {
        const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
        it = paramPtrCache.emplace(suffix, proc.apvts.getParameter(id)).first;
    }
    if (auto* p = it->second)
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void FilterSubsection::wireCallbacks()
{
    // Value display: number only. valueFromText still accepts "ms"/"s" suffixes for typed input.
    for (auto* k : { &filterAtk, &filterDec, &filterRel })
    {
        k->getSlider().textFromValueFunction = [](double v) { return adsrValueStr(v); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return parseAdsrTimeSec(s); };
    }
    // Sustain: 0-100, no unit in value.
    filterSus.getSlider().textFromValueFunction = [](double v) -> juce::String {
        return juce::String((int)std::round(v));
    };
    filterSus.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        return s.trim().dropLastCharacters(s.endsWith("%") ? 1 : 0).trim().getDoubleValue();
    };
    // Resonance + Depth: 0-100, no unit — display as integer.
    filterRes  .getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String((int)std::round(v)); };
    filterDepth.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String((int)std::round(v)); };

    // Cutoff: number only; unit lives in the label.
    filterCutoff.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v < 1000.0) return juce::String((int)std::round(v));
        return juce::String(v / 1000.0, 1);
    };
    filterCutoff.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        auto t = s.trim().toLowerCase();
        if (t.endsWith("khz")) return t.dropLastCharacters(3).trim().getDoubleValue() * 1000.0;
        if (t.endsWith("hz"))  return t.dropLastCharacters(2).trim().getDoubleValue();
        return t.getDoubleValue();
    };

    // Set initial dynamic labels. Single-letter A/D/S/R — universally understood,
    // gives the knob label room to render the unit suffix without ellipsis.
    filterCutoff.setLabel(cutoffLabelStr(filterCutoff.getValue()));
    filterAtk   .setLabel(adsrLabelStr("A", filterAtk.getValue()));
    filterDec   .setLabel(adsrLabelStr("D", filterDec.getValue()));
    filterSus   .setLabel("S (%)");
    filterRel   .setLabel(adsrLabelStr("R", filterRel.getValue()));

    // Default status bar callbacks (pass through formatted value).
    struct { KnobWithLabel* k; const char* name; } entries[] = {
        { &filterCutoff, "Filter Cutoff"           }, { &filterRes,   "Filter Resonance"       },
        { &filterAtk,    "Filter Envelope Attack"  }, { &filterDec,   "Filter Envelope Decay"  },
        { &filterSus,    "Filter Envelope Sustain" }, { &filterRel,   "Filter Envelope Release" },
        { &filterDepth,  "Filter Envelope Depth"   }, { &filterLowCut, "Filter Low Cut"        },
    };
    for (auto& e : entries)
    {
        juce::String n(e.name);
        e.k->onStatusUpdate = [this, n](const juce::String&, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate(n, val);
        };
    }

    // Per-knob status bar overrides: re-add unit since the value display no longer shows it.
    filterCutoff.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        const double v = filterCutoff.getValue();
        const juce::String fmt = (v < 1000.0) ? juce::String((int)std::round(v)) + " Hz"
                                               : juce::String(v / 1000.0, 1) + " kHz";
        if (onStatusUpdate) onStatusUpdate("Filter Cutoff", fmt);
    };
    filterAtk.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Filter Envelope Attack", formatAdsrTimeSec(filterAtk.getValue()));
    };
    filterDec.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Filter Envelope Decay", formatAdsrTimeSec(filterDec.getValue()));
    };
    filterSus.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Filter Envelope Sustain",
            juce::String((int)std::round(filterSus.getValue())) + "%");
    };
    filterRel.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Filter Envelope Release", formatAdsrTimeSec(filterRel.getValue()));
    };
    // Low Cut value display: "Off" when 0, else integer Hz / 1dp kHz.
    filterLowCut.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v <= 0.0)   return "Off";
        if (v < 1000.0) return juce::String((int)std::round(v));
        return juce::String(v / 1000.0, 2);
    };
    filterLowCut.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        const double v = filterLowCut.getValue();
        const juce::String fmt = v <= 0.0
                                   ? juce::String("Off")
                                   : (v < 1000.0 ? juce::String((int)std::round(v)) + " Hz"
                                                  : juce::String(v / 1000.0, 2) + " kHz");
        if (onStatusUpdate) onStatusUpdate("Filter Low Cut", fmt);
    };

    filterType.onChange = [this](int id) {
        apvtsSet("fltType", (float)(id - 1));
        if (onStatusUpdate) onStatusUpdate("Filter Type", filterType.getText());
    };
    filterCutoff.onValueChanged = [this](double v) {
        apvtsSet("fltCut", (float)v);
        filterCutoff.setLabel(cutoffLabelStr(v));
    };
    filterRes   .onValueChanged = [this](double v) { apvtsSet("fltRes",  (float)(v / 100.0)); };
    filterAtk   .onValueChanged = [this](double v) { apvtsSet("fEnvAtk", (float)v); filterAtk.setLabel(adsrLabelStr("A", v)); };
    filterDec   .onValueChanged = [this](double v) { apvtsSet("fEnvDec", (float)v); filterDec.setLabel(adsrLabelStr("D", v)); };
    filterSus   .onValueChanged = [this](double v) { apvtsSet("fEnvSus", (float)v); };
    filterRel   .onValueChanged = [this](double v) { apvtsSet("fEnvRel", (float)v); filterRel.setLabel(adsrLabelStr("R", v)); };
    filterDepth .onValueChanged = [this](double v) { apvtsSet("fEnvDep", (float)(v / 100.0 * 48.0)); };
    filterLowCut.onValueChanged = [this](double v) { apvtsSet("fltLoCut", (float)v); };

    filterLegCtrl.onChange = [this](int idx)
    {
        apvtsSet("fEnvLeg", idx > 0 ? 1.0f : 0.0f);
        if (onStatusUpdate) onStatusUpdate("Filter Env Legato", idx > 0 ? "On" : "Off");
    };
}

void FilterSubsection::setRhythm(int ri)
{
    if (ri != rhythmIndex)
        paramPtrCache.clear();
    rhythmIndex = ri;
    loadFromRhythm();
    refreshModulatedIndicators();
}

void FilterSubsection::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    filterType  .setSelectedId(p.filterType + 1, false);
    filterCutoff.setValue(p.filterCutoff,           dn);
    filterCutoff.setLabel(cutoffLabelStr(p.filterCutoff));
    filterRes   .setValue(p.filterRes * 100.0,      dn);
    filterAtk   .setValue(p.filterEnvAtk,           dn); filterAtk.setLabel(adsrLabelStr("A", p.filterEnvAtk));
    filterDec   .setValue(p.filterEnvDec,           dn); filterDec.setLabel(adsrLabelStr("D", p.filterEnvDec));
    filterSus   .setValue(p.filterEnvSus * 100.0,   dn);
    filterRel   .setValue(p.filterEnvRel,           dn); filterRel.setLabel(adsrLabelStr("R", p.filterEnvRel));
    filterDepth .setValue(p.filterEnvDepth / 48.0 * 100.0, dn);
    filterLowCut.setValue(p.filterLowCutHz, dn);
    filterLegCtrl.setSelectedIndex(p.filterEnvLegato ? 1 : 0);
}

void FilterSubsection::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    if      (suffix == "fltType") filterType  .setSelectedId(p.filterType + 1, false);
    else if (suffix == "fltCut")  { filterCutoff.setValue(p.filterCutoff, dn); filterCutoff.setLabel(cutoffLabelStr(p.filterCutoff)); }
    else if (suffix == "fltRes")  filterRes   .setValue(p.filterRes * 100.0,     dn);
    else if (suffix == "fEnvAtk") { filterAtk.setValue(p.filterEnvAtk, dn); filterAtk.setLabel(adsrLabelStr("A", p.filterEnvAtk)); }
    else if (suffix == "fEnvDec") { filterDec.setValue(p.filterEnvDec, dn); filterDec.setLabel(adsrLabelStr("D", p.filterEnvDec)); }
    else if (suffix == "fEnvSus") filterSus.setValue(p.filterEnvSus * 100.0, dn);
    else if (suffix == "fEnvRel") { filterRel.setValue(p.filterEnvRel, dn); filterRel.setLabel(adsrLabelStr("R", p.filterEnvRel)); }
    else if (suffix == "fEnvDep") filterDepth .setValue(p.filterEnvDepth / 48.0 * 100.0, dn);
    else if (suffix == "fltLoCut") filterLowCut.setValue(p.filterLowCutHz, dn);
    else if (suffix == "fEnvLeg")  filterLegCtrl.setSelectedIndex(p.filterEnvLegato ? 1 : 0);
}

void FilterSubsection::refreshModulatedIndicators()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& assigns = proc.getRhythm(rhythmIndex).modulationMatrix.getAssignments();

    auto isAssigned = [&assigns](const char* destId) -> bool {
        for (const auto& a : assigns)
            if (a.destinationId == destId) return true;
        return false;
    };

    auto sn = [&](int i) { return proc.getModSnapshot(rhythmIndex, i); };
    const float kNaN   = std::numeric_limits<float>::quiet_NaN();
    const bool playing = proc.sequencerPlaying.load();

    auto arc = [&](bool assigned, int idx) -> float {
        return (assigned && playing) ? sn(idx) : kNaN;
    };

    filterCutoff.setIsModulated(playing && isAssigned("filter.cutoff"));
    filterRes   .setIsModulated(playing && isAssigned("filter.resonance"));
    filterAtk   .setIsModulated(playing && isAssigned("fenv.attack"));
    filterDec   .setIsModulated(playing && isAssigned("fenv.decay"));
    filterDepth .setIsModulated(playing && isAssigned("fenv.depth"));

    // Filter Cutoff snapshot now stores ACTUAL Hz — use setModulatedActual
    // so the arc proportion goes through the slider's own midPoint skew
    // (matches the needle by construction). The other four destinations
    // still pre-normalise their snapshot to 0..1 with the linear/range that
    // matches their (also-linear) sliders, so setModulatedNorm stays correct.
    filterCutoff.setModulatedActual(arc(isAssigned("filter.cutoff"),    kSnapFilterCutoff));
    filterRes   .setModulatedNorm  (arc(isAssigned("filter.resonance"), kSnapFilterRes));
    filterAtk   .setModulatedNorm  (arc(isAssigned("fenv.attack"),      kSnapFenvAtk));
    filterDec   .setModulatedNorm  (arc(isAssigned("fenv.decay"),       kSnapFenvDec));
    filterDepth .setModulatedNorm  (arc(isAssigned("fenv.depth"),       kSnapFenvDepth));
}

void FilterSubsection::resized()
{
    // Voice section knobs render at Size 2 (55 × 56) — fixed PX, no
    // dependency on the panel's actual height.
    constexpr int kW    = MuClidLookAndFeel::kKnobSize2W;
    constexpr int rowH  = MuClidLookAndFeel::kKnobSize2H;
    constexpr int gap   = MuClidLookAndFeel::kVoiceGap;
    constexpr int row2Y = rowH + gap;

    using mu_ui::s;
    // Row 1: Type (2 cols) / Cutoff / Resonance / Low Cut.
    filterType  .setBounds(s(0 * kW), s(rowH / 4),     s(2 * kW), s(rowH / 2));
    filterCutoff.setBounds(s(2 * kW), 0,                s(kW),     s(rowH));
    filterRes   .setBounds(s(3 * kW), 0,                s(kW),     s(rowH));
    filterLowCut.setBounds(s(4 * kW), 0,                s(kW),     s(rowH));

    // Row 2 (envelope): A / D / S / R / [Depth stacked over Leg pill].
    // Both filter rows are full elsewhere so the env-legato pill stacks
    // beneath filterDepth in col 4 (Depth shrinks vertically; pill takes
    // the bottom strip). Tolerable size hit on Depth, none on the rest.
    filterAtk   .setBounds(s(0 * kW), s(row2Y), s(kW), s(rowH));
    filterDec   .setBounds(s(1 * kW), s(row2Y), s(kW), s(rowH));
    filterSus   .setBounds(s(2 * kW), s(row2Y), s(kW), s(rowH));
    filterRel   .setBounds(s(3 * kW), s(row2Y), s(kW), s(rowH));
    {
        constexpr int pillH    = 18;
        constexpr int stackGap = 2;
        const int depthH = s(rowH) - s(pillH) - s(stackGap);
        filterDepth .setBounds(s(4 * kW), s(row2Y), s(kW), depthH);
        filterLegCtrl.setBounds(s(4 * kW + 4),
                                 s(row2Y) + depthH + s(stackGap),
                                 s(kW - 8), s(pillH));
    }
}
