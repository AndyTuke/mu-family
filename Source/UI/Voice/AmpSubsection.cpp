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
} // namespace

AmpSubsection::AmpSubsection(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &ampLevel, &ampSendEff, &ampSendDly, &ampSendRev, &ampAccent,
                     &ampAtk, &ampDec, &ampSus, &ampRel })
        addAndMakeVisible(k);

    ampLevel  .setRange(0.0, 2.0,   0.01);  ampLevel  .setValue(0.5);
    ampSendEff.setRange(0.0, 1.0,  0.01);   ampSendEff.setValue(0.0);
    ampSendDly.setRange(0.0, 1.0,  0.01);   ampSendDly.setValue(0.0);
    ampSendRev.setRange(0.0, 1.0,  0.01);   ampSendRev.setValue(0.0);
    ampAccent .setRange(0.0, 12.0, 0.1);    ampAccent .setValue(0.0);
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
    for (auto* k : { &ampAtk, &ampDec })
    {
        k->getSlider().textFromValueFunction = [](double v) { return formatAdsrTimeSec(v); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return parseAdsrTimeSec(s); };
    }
    ampRel.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v >= 10.0) return "End";
        return formatAdsrTimeSec(v);
    };
    ampRel.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        if (s.trim().equalsIgnoreCase("end")) return 10.0;
        return parseAdsrTimeSec(s);
    };
    ampSus.getSlider().textFromValueFunction = [](double v) -> juce::String {
        return juce::String((int)std::round(v)) + "%";
    };
    ampSus.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        return s.trim().dropLastCharacters(s.endsWith("%") ? 1 : 0).trim().getDoubleValue();
    };

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

    ampLevel.onValueChanged = [this](double v) { apvtsSet("ampLvl",   (float)v); };
    ampAccent.onValueChanged = [this](double v) { apvtsSet("accentDb", (float)v); };
    ampAtk.onValueChanged    = [this](double v) { apvtsSet("aEnvAtk",  (float)v); };
    ampDec.onValueChanged    = [this](double v) { apvtsSet("aEnvDec",  (float)v); };
    ampSus.onValueChanged    = [this](double v) { apvtsSet("aEnvSus",  (float)v); };
    ampRel.onValueChanged    = [this](double v) { apvtsSet("aEnvRel",  (float)v); };

    ampSendEff.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendEff"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    ampSendDly.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendDly"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    ampSendRev.onValueChanged = [this](double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_sendRev"))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
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

    ampLevel.setValue(p.ampLevel,  dn);
    ampAccent.setValue(p.accentDb, dn);
    ampAtk.setValue(p.ampEnvAtk,   dn);
    ampDec.setValue(p.ampEnvDec,   dn);
    ampSus.setValue(p.ampEnvSus * 100.0, dn);
    ampRel.setValue(p.ampRelToEnd ? 10.0 : p.ampEnvRel, dn);

    const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
    auto load = [&](KnobWithLabel& k, const char* param) {
        if (auto* raw = proc.apvts.getRawParameterValue(chPfx + param))
            k.setValue(*raw, dn);
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

    if      (suffix == "ampLvl")   ampLevel .setValue(p.ampLevel,                              dn);
    else if (suffix == "accentDb") ampAccent.setValue(p.accentDb,                              dn);
    else if (suffix == "aEnvAtk")  ampAtk   .setValue(p.ampEnvAtk,                             dn);
    else if (suffix == "aEnvDec")  ampDec   .setValue(p.ampEnvDec,                             dn);
    else if (suffix == "aEnvSus")  ampSus   .setValue(p.ampEnvSus * 100.0,                     dn);
    else if (suffix == "aEnvRel")  ampRel   .setValue(p.ampRelToEnd ? 10.0 : p.ampEnvRel,      dn);
    else if (suffix == "sendEff" || suffix == "sendDly" || suffix == "sendRev")
    {
        const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
        if (auto* raw = proc.apvts.getRawParameterValue(chPfx + suffix))
        {
            if      (suffix == "sendEff") ampSendEff.setValue(*raw, dn);
            else if (suffix == "sendDly") ampSendDly.setValue(*raw, dn);
            else                          ampSendRev.setValue(*raw, dn);
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
    const int kW   = getWidth() / 5;
    const int gap  = 4;
    const int rowH = (getHeight() - gap) / 2;
    const int row2Y = rowH + gap;

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
