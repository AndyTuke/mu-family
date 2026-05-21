#include "PitchSubsection.h"
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
} // namespace

PitchSubsection::PitchSubsection(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &pitchOctave, &pitchSemi, &pitchFine,
                     &pitchAtk, &pitchDec, &pitchSus, &pitchRel, &pitchDepth })
        addAndMakeVisible(k);

    pitchOctave.setRange(-4.0,   4.0,   1.0);   pitchOctave.setValue(0.0);
    pitchSemi  .setRange(-12.0, 12.0,   1.0);   pitchSemi  .setValue(0.0);
    pitchFine  .setRange(-99.0, 99.0,   0.1);   pitchFine  .setValue(0.0);
    pitchAtk   .setRange(0.0, 10.0, 0.001);  pitchAtk.setValue(0.0);   pitchAtk.getSlider().setSkewFactor(0.3);
    pitchDec   .setRange(0.0, 10.0, 0.001);  pitchDec.setValue(0.03);  pitchDec.getSlider().setSkewFactor(0.3);
    pitchSus   .setRange(0.0, 100.0, 0.1);   pitchSus.setValue(0.0);
    pitchRel   .setRange(0.0, 10.0, 0.001);  pitchRel.setValue(0.03);  pitchRel.getSlider().setSkewFactor(0.3);
    pitchDepth .setRange(0.0, 100.0, 1.0);   pitchDepth.setValue(0.0);

    wireCallbacks();
}

void PitchSubsection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void PitchSubsection::wireCallbacks()
{
    for (auto* k : { &pitchAtk, &pitchDec, &pitchRel })
    {
        k->getSlider().textFromValueFunction = [](double v) { return adsrValueStr(v); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return parseAdsrTimeSec(s); };
    }
    pitchSus.getSlider().textFromValueFunction = [](double v) -> juce::String {
        return juce::String((int)std::round(v));
    };
    pitchSus.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        return s.trim().dropLastCharacters(s.endsWith("%") ? 1 : 0).trim().getDoubleValue();
    };

    // Set initial dynamic labels.
    pitchAtk.setLabel(adsrLabelStr("Attack",  pitchAtk.getValue()));
    pitchDec.setLabel(adsrLabelStr("Decay",   pitchDec.getValue()));
    pitchSus.setLabel("Sustain (%)");
    pitchRel.setLabel(adsrLabelStr("Release", pitchRel.getValue()));

    struct { KnobWithLabel* k; const char* name; } entries[] = {
        { &pitchOctave, "Pitch Octave"   }, { &pitchSemi,  "Pitch Semitone" },
        { &pitchFine,   "Pitch Fine"     }, { &pitchAtk,   "Pitch Attack"   },
        { &pitchDec,    "Pitch Decay"    }, { &pitchSus,   "Pitch Sustain"  },
        { &pitchRel,    "Pitch Release"  }, { &pitchDepth, "Pitch Depth"    },
    };
    for (auto& e : entries)
    {
        juce::String n(e.name);
        e.k->onStatusUpdate = [this, n](const juce::String&, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate(n, val);
        };
    }

    // Per-knob status bar overrides: re-add unit since the value display no longer shows it.
    pitchAtk.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Pitch Attack", formatAdsrTimeSec(pitchAtk.getValue()));
    };
    pitchDec.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Pitch Decay", formatAdsrTimeSec(pitchDec.getValue()));
    };
    pitchSus.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Pitch Sustain",
            juce::String((int)std::round(pitchSus.getValue())) + "%");
    };
    pitchRel.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Pitch Release", formatAdsrTimeSec(pitchRel.getValue()));
    };

    pitchOctave.onValueChanged = [this](double v) { apvtsSet("pitchOct",  (float)v); };
    pitchSemi  .onValueChanged = [this](double v) { apvtsSet("pitchSemi", (float)v); };
    pitchFine  .onValueChanged = [this](double v) { apvtsSet("pitchFine", (float)v); };
    pitchAtk   .onValueChanged = [this](double v) { apvtsSet("pEnvAtk",   (float)v); pitchAtk.setLabel(adsrLabelStr("Attack",  v)); };
    pitchDec   .onValueChanged = [this](double v) { apvtsSet("pEnvDec",   (float)v); pitchDec.setLabel(adsrLabelStr("Decay",   v)); };
    pitchSus   .onValueChanged = [this](double v) { apvtsSet("pEnvSus",   (float)v); };
    pitchRel   .onValueChanged = [this](double v) { apvtsSet("pEnvRel",   (float)v); pitchRel.setLabel(adsrLabelStr("Release", v)); };
    pitchDepth .onValueChanged = [this](double v) { apvtsSet("pEnvDep",   (float)(v / 100.0 * 24.0)); };
}

void PitchSubsection::setRhythm(int ri)
{
    rhythmIndex = ri;
    loadFromRhythm();
    refreshModulatedIndicators();
}

void PitchSubsection::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    pitchOctave.setValue(p.pitchOctave,          dn);
    pitchSemi  .setValue(p.pitchSemitones,       dn);
    pitchFine  .setValue(p.pitchFine,            dn);
    pitchAtk   .setValue(p.pitchEnvAtk,          dn); pitchAtk.setLabel(adsrLabelStr("Attack",  p.pitchEnvAtk));
    pitchDec   .setValue(p.pitchEnvDec,          dn); pitchDec.setLabel(adsrLabelStr("Decay",   p.pitchEnvDec));
    pitchSus   .setValue(p.pitchEnvSus * 100.0,  dn);
    pitchRel   .setValue(p.pitchEnvRel,          dn); pitchRel.setLabel(adsrLabelStr("Release", p.pitchEnvRel));
    pitchDepth .setValue(p.pitchEnvDepth / 24.0 * 100.0, dn);
}

void PitchSubsection::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    if      (suffix == "pitchOct")  pitchOctave.setValue(p.pitchOctave,         dn);
    else if (suffix == "pitchSemi") pitchSemi  .setValue(p.pitchSemitones,      dn);
    else if (suffix == "pitchFine") pitchFine  .setValue(p.pitchFine,           dn);
    else if (suffix == "pEnvAtk")   { pitchAtk.setValue(p.pitchEnvAtk, dn); pitchAtk.setLabel(adsrLabelStr("Attack",  p.pitchEnvAtk)); }
    else if (suffix == "pEnvDec")   { pitchDec.setValue(p.pitchEnvDec, dn); pitchDec.setLabel(adsrLabelStr("Decay",   p.pitchEnvDec)); }
    else if (suffix == "pEnvSus")   pitchSus   .setValue(p.pitchEnvSus * 100.0, dn);
    else if (suffix == "pEnvRel")   { pitchRel.setValue(p.pitchEnvRel, dn); pitchRel.setLabel(adsrLabelStr("Release", p.pitchEnvRel)); }
    else if (suffix == "pEnvDep")   pitchDepth .setValue(p.pitchEnvDepth / 24.0 * 100.0, dn);
}

void PitchSubsection::refreshModulatedIndicators()
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

    pitchOctave.setIsModulated(false);
    pitchSemi  .setIsModulated(playing && isAssigned("pitch.semitones"));
    pitchFine  .setIsModulated(false);
    pitchDepth .setIsModulated(playing && isAssigned("pitch.envDepth"));

    pitchOctave.setModulatedNorm(kNaN);
    pitchSemi  .setModulatedNorm(arc(isAssigned("pitch.semitones"), kSnapPitchSemi));
    pitchFine  .setModulatedNorm(kNaN);
    pitchDepth .setModulatedNorm(arc(isAssigned("pitch.envDepth"),  kSnapPitchEnvDep));
}

void PitchSubsection::resized()
{
    // Voice section knobs render at Size 2 (55 × 56) — fixed PX, no
    // dependency on the panel's actual height. See MuLookAndFeel.
    constexpr int kW    = MuClidLookAndFeel::kKnobSize2W;
    constexpr int rowH  = MuClidLookAndFeel::kKnobSize2H;
    constexpr int gap   = MuClidLookAndFeel::kVoiceGap;
    constexpr int row2Y = rowH + gap;

    pitchOctave.setBounds(0 * kW, 0,    kW, rowH);
    pitchSemi  .setBounds(1 * kW, 0,    kW, rowH);
    pitchFine  .setBounds(2 * kW, 0,    kW, rowH);
    pitchDepth .setBounds(3 * kW, 0,    kW, rowH);

    pitchAtk.setBounds(0 * kW, row2Y, kW, rowH);
    pitchDec.setBounds(1 * kW, row2Y, kW, rowH);
    pitchSus.setBounds(2 * kW, row2Y, kW, rowH);
    pitchRel.setBounds(3 * kW, row2Y, kW, rowH);
}
