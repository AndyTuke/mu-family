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

    // Pitch ranges per Andy's spec: octave ±3, semi ±12 (= ±1 octave), fine ±100 cents (1 cent step).
    // Combined max static pitch shift = ±4 octaves (3 oct + 1 oct + ≤1 semi from fine).
    pitchOctave.setRange(-3.0,   3.0,   1.0);   pitchOctave.setValue(0.0);
    pitchSemi  .setRange(-12.0, 12.0,   1.0);   pitchSemi  .setValue(0.0);
    pitchFine  .setRange(-100.0, 100.0, 1.0);   pitchFine  .setValue(0.0);
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
    auto it = paramPtrCache.find(suffix);
    if (it == paramPtrCache.end())
    {
        const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
        it = paramPtrCache.emplace(suffix, proc.apvts.getParameter(id)).first;
    }
    if (auto* p = it->second)
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

    // Set initial dynamic labels. Single-letter A/D/S/R — universally understood,
    // gives the knob label room to render the unit suffix without ellipsis.
    pitchAtk.setLabel(adsrLabelStr("A", pitchAtk.getValue()));
    pitchDec.setLabel(adsrLabelStr("D", pitchDec.getValue()));
    pitchSus.setLabel("S (%)");
    pitchRel.setLabel(adsrLabelStr("R", pitchRel.getValue()));

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
    pitchAtk   .onValueChanged = [this](double v) { apvtsSet("pEnvAtk",   (float)v); pitchAtk.setLabel(adsrLabelStr("A", v)); };
    pitchDec   .onValueChanged = [this](double v) { apvtsSet("pEnvDec",   (float)v); pitchDec.setLabel(adsrLabelStr("D", v)); };
    pitchSus   .onValueChanged = [this](double v) { apvtsSet("pEnvSus",   (float)v); };
    pitchRel   .onValueChanged = [this](double v) { apvtsSet("pEnvRel",   (float)v); pitchRel.setLabel(adsrLabelStr("R", v)); };
    pitchDepth .onValueChanged = [this](double v) { apvtsSet("pEnvDep",   (float)(v / 100.0 * 24.0)); };
}

void PitchSubsection::setRhythm(int ri)
{
    if (ri != rhythmIndex)
        paramPtrCache.clear();   // pointers were keyed to the prior rhythm's IDs
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
    pitchAtk   .setValue(p.pitchEnvAtk,          dn); pitchAtk.setLabel(adsrLabelStr("A", p.pitchEnvAtk));
    pitchDec   .setValue(p.pitchEnvDec,          dn); pitchDec.setLabel(adsrLabelStr("D", p.pitchEnvDec));
    pitchSus   .setValue(p.pitchEnvSus * 100.0,  dn);
    pitchRel   .setValue(p.pitchEnvRel,          dn); pitchRel.setLabel(adsrLabelStr("R", p.pitchEnvRel));
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
    else if (suffix == "pEnvAtk")   { pitchAtk.setValue(p.pitchEnvAtk, dn); pitchAtk.setLabel(adsrLabelStr("A", p.pitchEnvAtk)); }
    else if (suffix == "pEnvDec")   { pitchDec.setValue(p.pitchEnvDec, dn); pitchDec.setLabel(adsrLabelStr("D", p.pitchEnvDec)); }
    else if (suffix == "pEnvSus")   pitchSus   .setValue(p.pitchEnvSus * 100.0, dn);
    else if (suffix == "pEnvRel")   { pitchRel.setValue(p.pitchEnvRel, dn); pitchRel.setLabel(adsrLabelStr("R", p.pitchEnvRel)); }
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

    pitchOctave.setIsModulated(playing && isAssigned("pitch.octave"));
    pitchSemi  .setIsModulated(playing && isAssigned("pitch.semitones"));
    pitchFine  .setIsModulated(false);
    pitchDepth .setIsModulated(playing && isAssigned("pitch.envDepth"));

    // pitch.octave: snapshot stores (base octave + mod offset in octaves), slider is linear -4..+4.
    // setModulatedActual routes through valueToProportionOfLength so the arc aligns with the needle.
    pitchOctave.setModulatedActual(arc(isAssigned("pitch.octave"),     kSnapPitchOctave));
    // pitch.semitones: snapshot stores (base + offset) in semitones, slider is linear -12..+12.
    pitchSemi  .setModulatedActual(arc(isAssigned("pitch.semitones"),  kSnapPitchSemi));
    pitchFine  .setModulatedNorm(kNaN);
    // pitch.envDepth: voiceParams semis 0..24, slider display 0..100 — snapshot stores display 0..100 (#623).
    pitchDepth .setModulatedActual(arc(isAssigned("pitch.envDepth"),   kSnapPitchEnvDep));
}

void PitchSubsection::resized()
{
    // Voice section knobs render at Size 2 (55 × 56) — fixed PX, no
    // dependency on the panel's actual height. See MuLookAndFeel.
    constexpr int kW    = MuClidLookAndFeel::kKnobSize2W;
    constexpr int rowH  = MuClidLookAndFeel::kKnobSize2H;
    constexpr int gap   = MuClidLookAndFeel::kVoiceGap;
    constexpr int row2Y = rowH + gap;

    using mu_ui::s;
    // Row 1: Octave / Semi / Fine — col 4 left empty (the per-envelope
    // Leg pill was removed in #614; envelope legato is governed solely by
    // the hit-generator's pattern legato on EuclideanPanel's logic row).
    pitchOctave.setBounds(s(0 * kW), 0,        s(kW), s(rowH));
    pitchSemi  .setBounds(s(1 * kW), 0,        s(kW), s(rowH));
    pitchFine  .setBounds(s(2 * kW), 0,        s(kW), s(rowH));

    // Row 2 (envelope): A / D / S / R / Depth — the envelope depth control is
    // logically part of the envelope cluster so it now sits adjacent to A/D/S/R.
    pitchAtk  .setBounds(s(0 * kW), s(row2Y), s(kW), s(rowH));
    pitchDec  .setBounds(s(1 * kW), s(row2Y), s(kW), s(rowH));
    pitchSus  .setBounds(s(2 * kW), s(row2Y), s(kW), s(rowH));
    pitchRel  .setBounds(s(3 * kW), s(row2Y), s(kW), s(rowH));
    pitchDepth.setBounds(s(4 * kW), s(row2Y), s(kW), s(rowH));
}
