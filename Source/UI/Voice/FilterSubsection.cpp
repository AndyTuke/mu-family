#include "FilterSubsection.h"
#include "Plugin/PluginProcessor.h"
#include "Modulation/ModulationSnapshot.h"
#include "Sequencer/Rhythm.h"

namespace {
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
                     &filterAtk, &filterDec, &filterSus, &filterRel, &filterDepth })
        addAndMakeVisible(k);

    filterCutoff.setRange(20.0, 20000.0, 1.0);  filterCutoff.setValue(8000.0);
    filterCutoff.getSlider().setSkewFactorFromMidPoint(640.0);
    filterRes   .setRange(0.0,  100.0,  0.1);   filterRes   .setValue(20.0);
    filterAtk   .setRange(0.0,  10.0, 0.001);  filterAtk.setValue(0.03);  filterAtk.getSlider().setSkewFactor(0.3);
    filterDec   .setRange(0.0,  10.0, 0.001);  filterDec.setValue(0.09);  filterDec.getSlider().setSkewFactor(0.3);
    filterSus   .setRange(0.0, 100.0, 0.1);    filterSus.setValue(0.0);
    filterRel   .setRange(0.0,  10.0, 0.001);  filterRel.setValue(0.09);  filterRel.getSlider().setSkewFactor(0.3);
    filterDepth .setRange(0.0,   48.0,  0.1);  filterDepth.setValue(0.0);

    wireCallbacks();
}

void FilterSubsection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void FilterSubsection::wireCallbacks()
{
    for (auto* k : { &filterAtk, &filterDec, &filterRel })
    {
        k->getSlider().textFromValueFunction = [](double v) { return formatAdsrTimeSec(v); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return parseAdsrTimeSec(s); };
    }
    filterSus.getSlider().textFromValueFunction = [](double v) -> juce::String {
        return juce::String((int)std::round(v)) + "%";
    };
    filterSus.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        return s.trim().dropLastCharacters(s.endsWith("%") ? 1 : 0).trim().getDoubleValue();
    };

    filterCutoff.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v < 1000.0) return juce::String((int)std::round(v)) + " Hz";
        return juce::String(v / 1000.0, 1) + " kHz";
    };
    filterCutoff.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        auto t = s.trim().toLowerCase();
        if (t.endsWith("khz")) return t.dropLastCharacters(3).trim().getDoubleValue() * 1000.0;
        if (t.endsWith("hz"))  return t.dropLastCharacters(2).trim().getDoubleValue();
        return t.getDoubleValue();
    };

    struct { KnobWithLabel* k; const char* name; } entries[] = {
        { &filterCutoff, "Filter Cutoff"           }, { &filterRes,   "Filter Resonance"       },
        { &filterAtk,    "Filter Envelope Attack"  }, { &filterDec,   "Filter Envelope Decay"  },
        { &filterSus,    "Filter Envelope Sustain" }, { &filterRel,   "Filter Envelope Release" },
        { &filterDepth,  "Filter Envelope Depth"   },
    };
    for (auto& e : entries)
    {
        juce::String n(e.name);
        e.k->onStatusUpdate = [this, n](const juce::String&, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate(n, val);
        };
    }

    filterType.onChange = [this](int id) {
        apvtsSet("fltType", (float)(id - 1));
        if (onStatusUpdate) onStatusUpdate("Filter Type", filterType.getText());
    };
    filterCutoff.onValueChanged = [this](double v) { apvtsSet("fltCut", (float)v); };
    filterRes   .onValueChanged = [this](double v) { apvtsSet("fltRes", (float)(v / 100.0)); };
    filterAtk   .onValueChanged = [this](double v) { apvtsSet("fEnvAtk", (float)v); };
    filterDec   .onValueChanged = [this](double v) { apvtsSet("fEnvDec", (float)v); };
    filterSus   .onValueChanged = [this](double v) { apvtsSet("fEnvSus", (float)v); };
    filterRel   .onValueChanged = [this](double v) { apvtsSet("fEnvRel", (float)v); };
    filterDepth .onValueChanged = [this](double v) { apvtsSet("fEnvDep", (float)v); };
}

void FilterSubsection::setRhythm(int ri)
{
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
    filterRes   .setValue(p.filterRes * 100.0,      dn);
    filterAtk   .setValue(p.filterEnvAtk,           dn);
    filterDec   .setValue(p.filterEnvDec,           dn);
    filterSus   .setValue(p.filterEnvSus * 100.0,   dn);
    filterRel   .setValue(p.filterEnvRel,           dn);
    filterDepth .setValue(p.filterEnvDepth,         dn);
}

void FilterSubsection::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    if      (suffix == "fltType") filterType  .setSelectedId(p.filterType + 1, false);
    else if (suffix == "fltCut")  filterCutoff.setValue(p.filterCutoff,          dn);
    else if (suffix == "fltRes")  filterRes   .setValue(p.filterRes * 100.0,     dn);
    else if (suffix == "fEnvAtk") filterAtk   .setValue(p.filterEnvAtk,          dn);
    else if (suffix == "fEnvDec") filterDec   .setValue(p.filterEnvDec,          dn);
    else if (suffix == "fEnvSus") filterSus   .setValue(p.filterEnvSus * 100.0,  dn);
    else if (suffix == "fEnvRel") filterRel   .setValue(p.filterEnvRel,          dn);
    else if (suffix == "fEnvDep") filterDepth .setValue(p.filterEnvDepth,        dn);
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

    filterCutoff.setModulatedNorm(arc(isAssigned("filter.cutoff"),    kSnapFilterCutoff));
    filterRes   .setModulatedNorm(arc(isAssigned("filter.resonance"), kSnapFilterRes));
    filterAtk   .setModulatedNorm(arc(isAssigned("fenv.attack"),      kSnapFenvAtk));
    filterDec   .setModulatedNorm(arc(isAssigned("fenv.decay"),       kSnapFenvDec));
    filterDepth .setModulatedNorm(arc(isAssigned("fenv.depth"),       kSnapFenvDepth));
}

void FilterSubsection::resized()
{
    const int kW   = getWidth() / 5;
    const int gap  = 4;
    const int rowH = (getHeight() - gap) / 2;
    const int row2Y = rowH + gap;

    filterType  .setBounds(0 * kW, rowH / 4,     2 * kW, rowH / 2);
    filterCutoff.setBounds(2 * kW, 0,             kW,     rowH);
    filterRes   .setBounds(3 * kW, 0,             kW,     rowH);
    filterDepth .setBounds(4 * kW, 0,             kW,     rowH);

    filterAtk.setBounds(1 * kW, row2Y, kW, rowH);
    filterDec.setBounds(2 * kW, row2Y, kW, rowH);
    filterSus.setBounds(3 * kW, row2Y, kW, rowH);
    filterRel.setBounds(4 * kW, row2Y, kW, rowH);
}
