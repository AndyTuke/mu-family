#include "AmpSubsection.h"
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

AmpSubsection::AmpSubsection(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &ampLevel, &ampSendEff, &ampSendDly, &ampSendRev, &ampAccent,
                     &ampAtk, &ampDec, &ampSus, &ampRel })
        addAndMakeVisible(k);

    ampLevel  .setRange(-60.0, 6.0,  0.1);  ampLevel  .setValue(0.0);
    ampSendEff.setRange(0.0, 100.0, 1.0);   ampSendEff.setValue(0.0);
    ampSendDly.setRange(0.0, 100.0, 1.0);   ampSendDly.setValue(0.0);
    ampSendRev.setRange(0.0, 100.0, 1.0);   ampSendRev.setValue(0.0);
    ampAccent .setRange(0.0, 100.0, 1.0);   ampAccent .setValue(0.0);
    ampAtk    .setRange(0.0, 10.0,  0.001); ampAtk .setValue(0.005); ampAtk.getSlider().setSkewFactor(0.3);
    ampDec    .setRange(0.0, 10.0,  0.001); ampDec .setValue(0.3);   ampDec.getSlider().setSkewFactor(0.3);
    ampSus    .setRange(0.0, 100.0, 0.1);   ampSus .setValue(80.0);
    ampRel    .setRange(0.0, 10.0,  0.001); ampRel .setValue(0.5);   ampRel.getSlider().setSkewFactor(0.3);

    wireCallbacks();
}

void AmpSubsection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

void AmpSubsection::wireCallbacks()
{
    ampLevel.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v <= -60.0) return "-inf";
        return juce::String(v, 1);
    };
    ampLevel.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        auto t = s.trim().toLowerCase();
        if (t.startsWith("-inf")) return -60.0;
        if (t.endsWith("db"))    return t.dropLastCharacters(2).trim().getDoubleValue();
        return t.getDoubleValue();
    };

    for (auto* k : { &ampAtk, &ampDec })
    {
        k->getSlider().textFromValueFunction = [](double v) { return adsrValueStr(v); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return parseAdsrTimeSec(s); };
    }
    ampRel.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v >= 10.0) return "End";
        return adsrValueStr(v);
    };
    ampRel.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        if (s.trim().equalsIgnoreCase("end")) return 10.0;
        return parseAdsrTimeSec(s);
    };
    ampSus.getSlider().textFromValueFunction = [](double v) -> juce::String {
        return juce::String((int)std::round(v));
    };
    ampSus.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        return s.trim().dropLastCharacters(s.endsWith("%") ? 1 : 0).trim().getDoubleValue();
    };

    // Set initial dynamic labels.
    ampAtk.setLabel(adsrLabelStr("Attack",  ampAtk.getValue()));
    ampDec.setLabel(adsrLabelStr("Decay",   ampDec.getValue()));
    ampSus.setLabel("Sustain (%)");
    ampRel.setLabel(ampRel.getValue() >= 10.0 ? "Release (s)" : adsrLabelStr("Release", ampRel.getValue()));

    struct { KnobWithLabel* k; const char* name; } entries[] = {
        { &ampLevel,   "Amp Level"   }, { &ampSendEff, "Amp Send Effect" },
        { &ampSendDly, "Amp Send Delay" }, { &ampSendRev, "Amp Send Reverb" },
        { &ampAccent,  "Amp Accent"  }, { &ampAtk,     "Amp Attack"     },
        { &ampDec,     "Amp Decay"   }, { &ampSus,     "Amp Sustain"    },
        { &ampRel,     "Amp Release" },
    };
    for (auto& e : entries)
    {
        juce::String n(e.name);
        e.k->onStatusUpdate = [this, n](const juce::String&, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate(n, val);
        };
    }

    // Per-knob status bar overrides: re-add unit since the value display no longer shows it.
    ampLevel.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        const double v = ampLevel.getValue();
        const juce::String fmt = v <= -60.0 ? "-inf dB" : juce::String(v, 1) + " dB";
        if (onStatusUpdate) onStatusUpdate("Amp Level", fmt);
    };
    ampAtk.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Amp Attack", formatAdsrTimeSec(ampAtk.getValue()));
    };
    ampDec.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Amp Decay", formatAdsrTimeSec(ampDec.getValue()));
    };
    ampSus.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Amp Sustain",
            juce::String((int)std::round(ampSus.getValue())) + "%");
    };
    ampRel.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        const double v = ampRel.getValue();
        if (onStatusUpdate) onStatusUpdate("Amp Release", v >= 10.0 ? "End" : formatAdsrTimeSec(v));
    };

    ampLevel.onValueChanged = [this](double v) { apvtsSet("ampLvl", juce::Decibels::decibelsToGain((float)v, -60.0f)); };
    ampAccent.onValueChanged = [this](double v) { apvtsSet("accentDb", (float)(v / 100.0 * 12.0)); };
    ampAtk.onValueChanged    = [this](double v) { apvtsSet("aEnvAtk",  (float)v); ampAtk.setLabel(adsrLabelStr("Attack", v)); };
    ampDec.onValueChanged    = [this](double v) { apvtsSet("aEnvDec",  (float)v); ampDec.setLabel(adsrLabelStr("Decay",  v)); };
    ampSus.onValueChanged    = [this](double v) { apvtsSet("aEnvSus",  (float)v); };
    ampRel.onValueChanged    = [this](double v) { apvtsSet("aEnvRel",  (float)v); ampRel.setLabel(v >= 10.0 ? "Release (s)" : adsrLabelStr("Release", v)); };

    ampSendEff.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendEff"))
            p->setValueNotifyingHost(p->convertTo0to1((float)(v / 100.0)));
    };
    ampSendDly.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendDly"))
            p->setValueNotifyingHost(p->convertTo0to1((float)(v / 100.0)));
    };
    ampSendRev.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendRev"))
            p->setValueNotifyingHost(p->convertTo0to1((float)(v / 100.0)));
    };
}

void AmpSubsection::setRhythm(int ri)
{
    rhythmIndex = ri;
    loadFromRhythm();
    refreshModulatedIndicators();
}

void AmpSubsection::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    ampLevel.setValue(juce::Decibels::gainToDecibels(p.ampLevel, -60.0f), dn);
    ampAccent.setValue(p.accentDb / 12.0 * 100.0, dn);
    ampAtk.setValue(p.ampEnvAtk,   dn); ampAtk.setLabel(adsrLabelStr("Attack", p.ampEnvAtk));
    ampDec.setValue(p.ampEnvDec,   dn); ampDec.setLabel(adsrLabelStr("Decay",  p.ampEnvDec));
    ampSus.setValue(p.ampEnvSus * 100.0, dn);
    { const double relV = p.ampRelToEnd ? 10.0 : p.ampEnvRel;
      ampRel.setValue(relV, dn); ampRel.setLabel(relV >= 10.0 ? "Release (s)" : adsrLabelStr("Release", relV)); }

    const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
    auto load = [&](KnobWithLabel& k, const char* param) {
        if (auto* raw = proc.apvts.getRawParameterValue(chPfx + param))
            k.setValue(*raw * 100.0, dn);
    };
    load(ampSendEff, "sendEff");
    load(ampSendDly, "sendDly");
    load(ampSendRev, "sendRev");
}

void AmpSubsection::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    if      (suffix == "ampLvl")   ampLevel .setValue(juce::Decibels::gainToDecibels(p.ampLevel, -60.0f), dn);
    else if (suffix == "accentDb") ampAccent.setValue(p.accentDb / 12.0 * 100.0,               dn);
    else if (suffix == "aEnvAtk")  { ampAtk.setValue(p.ampEnvAtk, dn); ampAtk.setLabel(adsrLabelStr("Attack", p.ampEnvAtk)); }
    else if (suffix == "aEnvDec")  { ampDec.setValue(p.ampEnvDec, dn); ampDec.setLabel(adsrLabelStr("Decay",  p.ampEnvDec)); }
    else if (suffix == "aEnvSus")  ampSus   .setValue(p.ampEnvSus * 100.0,                     dn);
    else if (suffix == "aEnvRel")  { const double rv = p.ampRelToEnd ? 10.0 : p.ampEnvRel; ampRel.setValue(rv, dn); ampRel.setLabel(rv >= 10.0 ? "Release (s)" : adsrLabelStr("Release", rv)); }
    else if (suffix == "sendEff" || suffix == "sendDly" || suffix == "sendRev")
    {
        const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
        if (auto* raw = proc.apvts.getRawParameterValue(chPfx + suffix))
        {
            if      (suffix == "sendEff") ampSendEff.setValue(*raw * 100.0, dn);
            else if (suffix == "sendDly") ampSendDly.setValue(*raw * 100.0, dn);
            else                          ampSendRev.setValue(*raw * 100.0, dn);
        }
    }
}

void AmpSubsection::refreshModulatedIndicators()
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

    ampAtk   .setIsModulated(playing && isAssigned("amp.attack"));
    ampDec   .setIsModulated(playing && isAssigned("amp.decay"));
    ampSus   .setIsModulated(playing && isAssigned("amp.sustain"));
    ampRel   .setIsModulated(playing && isAssigned("amp.release"));
    ampLevel .setIsModulated(playing && isAssigned("amp.level"));
    ampAccent.setIsModulated(playing && isAssigned("accentDb"));

    ampAtk   .setModulatedNorm(arc(isAssigned("amp.attack"),   kSnapAmpAtk));
    ampDec   .setModulatedNorm(arc(isAssigned("amp.decay"),    kSnapAmpDec));
    ampSus   .setModulatedNorm(arc(isAssigned("amp.sustain"),  kSnapAmpSus));
    ampRel   .setModulatedNorm(arc(isAssigned("amp.release"),  kSnapAmpRel));
    ampLevel .setModulatedNorm(arc(isAssigned("amp.level"),    kSnapAmpLvl));
    ampAccent.setModulatedNorm(arc(isAssigned("accentDb"),     kSnapAccent));
}

void AmpSubsection::resized()
{
    constexpr int kW   = MuClidLookAndFeel::kVoicePFAKnobW;
    constexpr int gap  = MuClidLookAndFeel::kVoiceGap;
    constexpr int rowH = MuClidLookAndFeel::kVoiceKnobCellH;
    constexpr int row2Y = rowH + gap;

    ampLevel  .setBounds(0 * kW, 0,    kW, rowH);
    ampSendEff.setBounds(1 * kW, 0,    kW, rowH);
    ampSendDly.setBounds(2 * kW, 0,    kW, rowH);
    ampSendRev.setBounds(3 * kW, 0,    kW, rowH);
    ampAccent .setBounds(4 * kW, 0,    kW, rowH);

    ampAtk.setBounds(0 * kW, row2Y, kW, rowH);
    ampDec.setBounds(1 * kW, row2Y, kW, rowH);
    ampSus.setBounds(2 * kW, row2Y, kW, rowH);
    ampRel.setBounds(3 * kW, row2Y, kW, rowH);
}
