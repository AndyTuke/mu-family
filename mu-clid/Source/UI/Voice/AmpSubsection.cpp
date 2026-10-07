#include "AmpSubsection.h"
#include "Plugin/PluginProcessor.h"
#include "ValueFormat.h"   // mu-core: shared value text
#include "Modulation/ModulationSnapshot.h"
#include "Sequencer/Rhythm.h"

AmpSubsection::AmpSubsection(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &ampLevel, &ampSendEff, &ampSendDly, &ampSendRev, &ampAccent, &ampPan,
                     &ampAtk, &ampDec, &ampSus, &ampRel })
        addAndMakeVisible(k);

    // Slider ranges match APVTS units 1:1 (Step 0) — no conversion lambdas
    // in onValueChanged / loadFromRhythm. ampLevel + accent are in dB; sends use
    // 0..1 normalised matching the mixer-channel sends + APVTS ch*_send* params.
    ampLevel  .setRange(-60.0, 6.0,  0.1);  ampLevel  .setValue(0.0);
    ampSendEff.setRange(0.0, 1.0, 0.01);    ampSendEff.setValue(0.0);
    ampSendDly.setRange(0.0, 1.0, 0.01);    ampSendDly.setValue(0.0);
    ampSendRev.setRange(0.0, 1.0, 0.01);    ampSendRev.setValue(0.0);
    ampAccent .setRange(0.0, 12.0, 0.1);    ampAccent .setValue(0.0);
    // Pan as on the mixer strip: -1 (L) … +1 (R), no value text on the knob.
    ampPan    .setRange(-1.0, 1.0, 0.01);   ampPan    .setValue(0.0);
    ampPan.getSlider().textFromValueFunction = [](double) { return juce::String(); };
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
        k->getSlider().textFromValueFunction = [](double v) { return mu_fmt::time(v, false); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return mu_fmt::parseTime(s); };
    }
    ampRel.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v >= 10.0) return "End";
        return mu_fmt::time(v, false);
    };
    ampRel.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        if (s.trim().equalsIgnoreCase("end")) return 10.0;
        return mu_fmt::parseTime(s);
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
        if (onStatusUpdate) onStatusUpdate("Amp Attack", mu_fmt::time(ampAtk.getValue()));
    };
    ampDec.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Amp Decay", mu_fmt::time(ampDec.getValue()));
    };
    ampSus.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        if (onStatusUpdate) onStatusUpdate("Amp Sustain",
            juce::String((int)std::round(ampSus.getValue())) + "%");
    };
    ampRel.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        const double v = ampRel.getValue();
        if (onStatusUpdate) onStatusUpdate("Amp Release", v >= 10.0 ? "End" : mu_fmt::time(v));
    };
    ampPan.onStatusUpdate = [this](const juce::String&, const juce::String&) {
        const int v = (int) std::round(ampPan.getValue() * 100.0);
        if (onStatusUpdate) onStatusUpdate("Pan", v == 0 ? juce::String("C")
                                                         : (v < 0 ? "L " : "R ") + juce::String(std::abs(v)));
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

    // Slider value == APVTS value (Step 0) — no conversion in the lambdas.
    ampLevel.onValueChanged  = [this](double v) { apvtsSet("ampLvl",   (float)v); };
    ampAccent.onValueChanged = [this](double v) { apvtsSet("accentDb", (float)v); };
    ampAtk.onValueChanged    = [this](double v) { apvtsSet("aEnvAtk",  (float)v); };
    ampDec.onValueChanged    = [this](double v) { apvtsSet("aEnvDec",  (float)v); };
    ampSus.onValueChanged    = [this](double v) { apvtsSet("aEnvSus",  (float)v); };
    ampRel.onValueChanged    = [this](double v) { apvtsSet("aEnvRel",  (float)v); };

    auto writeChannelSend = [this](const char* suffix, double v) {
        if (rhythmIndex < 0) return;
        if (auto* p = proc.apvts.getParameter("ch" + juce::String(rhythmIndex) + "_" + suffix))
            p->setValueNotifyingHost(p->convertTo0to1((float)v));
    };
    ampSendEff.onValueChanged = [writeChannelSend](double v) { writeChannelSend("sendEff", v); };
    ampSendDly.onValueChanged = [writeChannelSend](double v) { writeChannelSend("sendDly", v); };
    ampSendRev.onValueChanged = [writeChannelSend](double v) { writeChannelSend("sendRev", v); };
    ampPan    .onValueChanged = [writeChannelSend](double v) { writeChannelSend("pan",     v); };
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

    ampLevel.setValue(p.ampLevel,  dn);          // dB already (Step 0)
    ampAccent.setValue(p.accentDb, dn);          // dB already
    ampAtk.setValue(p.ampEnvAtk,   dn);    ampDec.setValue(p.ampEnvDec,   dn);    ampSus.setValue(p.ampEnvSus * 100.0, dn);    // voiceParams stores 0..1; APVTS + slider are 0..100 (data-layer scaling)
    { const double relV = p.ampRelToEnd ? 10.0 : p.ampEnvRel;
      ampRel.setValue(relV, dn); }

    const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
    auto load = [&](KnobWithLabel& k, const char* param) {
        if (auto* raw = proc.apvts.getRawParameterValue(chPfx + param))
            k.setValue(*raw, dn);
    };
    load(ampSendEff, "sendEff");
    load(ampSendDly, "sendDly");
    load(ampSendRev, "sendRev");
    load(ampPan,     "pan");
}

void AmpSubsection::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    if      (suffix == "ampLvl")   ampLevel .setValue(p.ampLevel,  dn);
    else if (suffix == "accentDb") ampAccent.setValue(p.accentDb,  dn);
    else if (suffix == "aEnvAtk")  { ampAtk.setValue(p.ampEnvAtk, dn); }
    else if (suffix == "aEnvDec")  { ampDec.setValue(p.ampEnvDec, dn); }
    else if (suffix == "aEnvSus")  ampSus   .setValue(p.ampEnvSus * 100.0,                     dn);
    else if (suffix == "aEnvRel")  { const double rv = p.ampRelToEnd ? 10.0 : p.ampEnvRel; ampRel.setValue(rv, dn); }
    else if (suffix == "sendEff" || suffix == "sendDly" || suffix == "sendRev" || suffix == "pan")
    {
        const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
        if (auto* raw = proc.apvts.getRawParameterValue(chPfx + suffix))
        {
            if      (suffix == "sendEff") ampSendEff.setValue(*raw, dn);
            else if (suffix == "sendDly") ampSendDly.setValue(*raw, dn);
            else if (suffix == "sendRev") ampSendRev.setValue(*raw, dn);
            else                          ampPan    .setValue(*raw, dn);
        }
    }
}

void AmpSubsection::bindModulationIndicators()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms())
    {
        for (auto* k : { &ampAtk, &ampDec, &ampSus, &ampLevel, &ampAccent,
                         &ampPan, &ampSendEff, &ampSendDly, &ampSendRev })
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
    // Mixer strip: pan + FX sends — snap stores the actual value (pan -1..+1, sends 0..1).
    struct { KnobWithLabel* k; const char* dest; ModSnapIdx snap; } strip[] = {
        { &ampPan,     "amp.pan",     kSnapPan     }, { &ampSendEff, "send.effect", kSnapSendEff },
        { &ampSendDly, "send.delay",  kSnapSendDly }, { &ampSendRev, "send.reverb", kSnapSendRev },
    };
    for (auto& e : strip)
        e.k->bindModulation(e.dest, mx,
            [&proc = proc, ri = rhythmIndex, snap = e.snap]() -> float {
                return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, snap) : kNaN; });
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
    // Row 1: Level / Accent (both shape the amplitude per hit), Pan top right. The FX
    // sends are placed by the host, beside the insert dropdown.
    ampLevel  .setBounds(s(0 * kW), 0,        s(kW), s(rowH));
    ampAccent .setBounds(s(1 * kW), 0,        s(kW), s(rowH));
    ampPan    .setBounds(s(3 * kW), 0,        s(kW), s(rowH));

    ampAtk.setBounds(s(0 * kW), s(row2Y), s(kW), s(rowH));
    ampDec.setBounds(s(1 * kW), s(row2Y), s(kW), s(rowH));
    ampSus.setBounds(s(2 * kW), s(row2Y), s(kW), s(rowH));
    ampRel.setBounds(s(3 * kW), s(row2Y), s(kW), s(rowH));
}
