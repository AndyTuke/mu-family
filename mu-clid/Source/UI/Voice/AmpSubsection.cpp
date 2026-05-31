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

    // Slider ranges match APVTS units 1:1 (#598 Step 0) — no conversion lambdas
    // in onValueChanged / loadFromRhythm. ampLevel + accent are in dB; sends use
    // 0..1 normalised matching the mixer-channel sends + APVTS ch*_send* params.
    ampLevel  .setRange(-60.0, 6.0,  0.1);  ampLevel  .setValue(0.0);
    ampSendEff.setRange(0.0, 1.0, 0.01);    ampSendEff.setValue(0.0);
    ampSendDly.setRange(0.0, 1.0, 0.01);    ampSendDly.setValue(0.0);
    ampSendRev.setRange(0.0, 1.0, 0.01);    ampSendRev.setValue(0.0);
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
    auto it = paramPtrCache.find(suffix);
    if (it == paramPtrCache.end())
    {
        const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
        it = paramPtrCache.emplace(suffix, proc.apvts.getParameter(id)).first;
    }
    if (auto* p = it->second)
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

    // Accent: dB-domain slider (0..12), one decimal place.
    ampAccent.getSlider().textFromValueFunction = [](double v) -> juce::String { return juce::String(v, 1); };
    // Sends: slider is 0..1 normalised; show as a 0..100 integer percentage so users
    // still read the familiar "75" — the underlying knob value remains 0..1 to match APVTS.
    auto sendText = [](double v) -> juce::String { return juce::String((int)std::round(v * 100.0)); };
    auto sendParse = [](const juce::String& s) -> double {
        const auto t = s.trim().dropLastCharacters(s.endsWith("%") ? 1 : 0).trim();
        return t.getDoubleValue() / 100.0;
    };
    for (auto* k : { &ampSendEff, &ampSendDly, &ampSendRev })
    {
        k->getSlider().textFromValueFunction = sendText;
        k->getSlider().valueFromTextFunction = sendParse;
    }

    // Set initial dynamic labels. Single-letter A/D/S/R — universally understood,
    // gives the knob label room to render the unit suffix without ellipsis.
    ampAtk.setLabel(adsrLabelStr("A", ampAtk.getValue()));
    ampDec.setLabel(adsrLabelStr("D", ampDec.getValue()));
    ampSus.setLabel("S (%)");
    ampRel.setLabel(ampRel.getValue() >= 10.0 ? "R (s)" : adsrLabelStr("R", ampRel.getValue()));

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
    ampAccent.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Amp Accent", juce::String(ampAccent.getValue(), 1) + " dB");
    };
    {
        struct { KnobWithLabel* k; const char* name; } sends[] = {
            { &ampSendEff, "Amp Send Effect" }, { &ampSendDly, "Amp Send Delay" }, { &ampSendRev, "Amp Send Reverb" }
        };
        for (auto& e : sends)
        {
            juce::String n(e.name);
            e.k->onStatusUpdate = [this, k = e.k, n](const juce::String&, const juce::String&) {
                if (onStatusUpdate) onStatusUpdate(n, juce::String((int)std::round(k->getValue() * 100.0)) + "%");
            };
        }
    }

    // Slider value == APVTS value (#598 Step 0) — no conversion in the lambdas.
    ampLevel.onValueChanged  = [this](double v) { apvtsSet("ampLvl",   (float)v); };
    ampAccent.onValueChanged = [this](double v) { apvtsSet("accentDb", (float)v); };
    ampAtk.onValueChanged    = [this](double v) { apvtsSet("aEnvAtk",  (float)v); ampAtk.setLabel(adsrLabelStr("A", v)); };
    ampDec.onValueChanged    = [this](double v) { apvtsSet("aEnvDec",  (float)v); ampDec.setLabel(adsrLabelStr("D", v)); };
    ampSus.onValueChanged    = [this](double v) { apvtsSet("aEnvSus",  (float)v); };
    ampRel.onValueChanged    = [this](double v) { apvtsSet("aEnvRel",  (float)v); ampRel.setLabel(v >= 10.0 ? "R (s)" : adsrLabelStr("R", v)); };

    auto writeChannelSend = [this](const char* suffix, double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_" + suffix))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    ampSendEff.onValueChanged = [writeChannelSend](double v) { writeChannelSend("sendEff", v); };
    ampSendDly.onValueChanged = [writeChannelSend](double v) { writeChannelSend("sendDly", v); };
    ampSendRev.onValueChanged = [writeChannelSend](double v) { writeChannelSend("sendRev", v); };
}

void AmpSubsection::setRhythm(int ri)
{
    if (ri != rhythmIndex)
        paramPtrCache.clear();
    rhythmIndex = ri;
    loadFromRhythm();
    bindModulationIndicators();
}

void AmpSubsection::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    ampLevel.setValue(p.ampLevel,  dn);          // dB already (#598 Step 0)
    ampAccent.setValue(p.accentDb, dn);          // dB already
    ampAtk.setValue(p.ampEnvAtk,   dn); ampAtk.setLabel(adsrLabelStr("A", p.ampEnvAtk));
    ampDec.setValue(p.ampEnvDec,   dn); ampDec.setLabel(adsrLabelStr("D", p.ampEnvDec));
    ampSus.setValue(p.ampEnvSus * 100.0, dn);    // voiceParams stores 0..1; APVTS + slider are 0..100 (data-layer scaling)
    { const double relV = p.ampRelToEnd ? 10.0 : p.ampEnvRel;
      ampRel.setValue(relV, dn); ampRel.setLabel(relV >= 10.0 ? "R (s)" : adsrLabelStr("R", relV)); }

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

    if      (suffix == "ampLvl")   ampLevel .setValue(p.ampLevel,  dn);
    else if (suffix == "accentDb") ampAccent.setValue(p.accentDb,  dn);
    else if (suffix == "aEnvAtk")  { ampAtk.setValue(p.ampEnvAtk, dn); ampAtk.setLabel(adsrLabelStr("A", p.ampEnvAtk)); }
    else if (suffix == "aEnvDec")  { ampDec.setValue(p.ampEnvDec, dn); ampDec.setLabel(adsrLabelStr("D", p.ampEnvDec)); }
    else if (suffix == "aEnvSus")  ampSus   .setValue(p.ampEnvSus * 100.0,                     dn);
    else if (suffix == "aEnvRel")  { const double rv = p.ampRelToEnd ? 10.0 : p.ampEnvRel; ampRel.setValue(rv, dn); ampRel.setLabel(rv >= 10.0 ? "R (s)" : adsrLabelStr("R", rv)); }
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

void AmpSubsection::bindModulationIndicators()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms())
    {
        for (auto* k : { &ampAtk, &ampDec, &ampSus, &ampLevel, &ampAccent })
            k->clearModBinding();
        return;
    }
    const auto* mx = &proc.getRhythm(rhythmIndex).modulationMatrix;
    static const float kNaN = std::numeric_limits<float>::quiet_NaN();

    // ADSR attack/decay: snap stores ACTUAL seconds (skewFactor 0.3 slider) → setModulatedActual.
    ampAtk.bindModulation("amp.attack", mx,
        [&proc = proc, ri = rhythmIndex]() -> float {
            return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, kSnapAmpAtk) : kNaN; });
    ampDec.bindModulation("amp.decay", mx,
        [&proc = proc, ri = rhythmIndex]() -> float {
            return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, kSnapAmpDec) : kNaN; });
    // Sustain: snap normalised 0..1, slider linear 0..100 → normMode.
    ampSus.bindModulation("amp.sustain", mx,
        [&proc = proc, ri = rhythmIndex]() -> float {
            return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, kSnapAmpSus) : kNaN; },
        true);
    // amp.level: snap stores actual dB (-60..+6).
    ampLevel.bindModulation("amp.level", mx,
        [&proc = proc, ri = rhythmIndex]() -> float {
            return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, kSnapAmpLvl) : kNaN; });
    // accentDb: snap stores display 0..100 dB.
    ampAccent.bindModulation("accentDb", mx,
        [&proc = proc, ri = rhythmIndex]() -> float {
            return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, kSnapAccent) : kNaN; });
    // ampRel: Release is not a modulation target — leave unbound.
}

void AmpSubsection::setEffectSendLabel(const juce::String& name)
{
    ampSendEff.setLabel(name);
}

void AmpSubsection::resized()
{
    // Voice section knobs render at Size 2 (55 × 56) — fixed PX, no
    // dependency on the panel's actual height.
    constexpr int kW    = MuLookAndFeel::kKnobSize2W;
    constexpr int rowH  = MuLookAndFeel::kKnobSize2H;
    constexpr int gap   = MuLookAndFeel::kVoiceGap;
    constexpr int row2Y = rowH + gap;

    using mu_ui::s;
    // Row 1: Level / Accent / Effect / Delay / Reverb — Accent sits next to
    // Level (both shape the amplitude per-hit) before the FX-send cluster.
    ampLevel  .setBounds(s(0 * kW), 0,        s(kW), s(rowH));
    ampAccent .setBounds(s(1 * kW), 0,        s(kW), s(rowH));
    ampSendEff.setBounds(s(2 * kW), 0,        s(kW), s(rowH));
    ampSendDly.setBounds(s(3 * kW), 0,        s(kW), s(rowH));
    ampSendRev.setBounds(s(4 * kW), 0,        s(kW), s(rowH));

    ampAtk.setBounds(s(0 * kW), s(row2Y), s(kW), s(rowH));
    ampDec.setBounds(s(1 * kW), s(row2Y), s(kW), s(rowH));
    ampSus.setBounds(s(2 * kW), s(row2Y), s(kW), s(rowH));
    ampRel.setBounds(s(3 * kW), s(row2Y), s(kW), s(rowH));
    // col 4 of row 2 left empty (was the env-legato pill, removed in #614).
}
