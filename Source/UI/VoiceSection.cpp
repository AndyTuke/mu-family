#include "VoiceSection.h"
#include "../PluginProcessor.h"

VoiceSection::VoiceSection(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &pitchOctave, &pitchSemi, &pitchFine,
                     &pitchAtk, &pitchDec, &pitchSus, &pitchRel, &pitchDepth,
                     &filterCutoff, &filterRes,
                     &filterAtk, &filterDec, &filterSus, &filterRel, &filterDepth,
                     &ampLevel, &ampSendEff, &ampSendDly, &ampSendRev, &ampAccent,
                     &ampAtk, &ampDec, &ampSus, &ampRel,
                     &driveDrive, &driveOutput, &driveDither, &driveTone })
        addAndMakeVisible(k);

    // Filter type dropdown — item ID = filterType + 1; selectedId - 1 = filterType value.
    // Types 0-3,9: SVF. Types 4-6,10: LadderFilter. 7: 1-pole LP. 11: 1-pole HP. 8: Comb. 12-14: Biquad EQ.
    filterType.addItem("LP 6",    8);   // type 7
    filterType.addItem("LP 12",   1);   // type 0
    filterType.addItem("LP 24",   5);   // type 4
    filterType.addItem("BP 12",   3);   // type 2
    filterType.addItem("BP 24",   7);   // type 6
    filterType.addItem("HP 6",    12);  // type 11
    filterType.addItem("HP 12",   2);   // type 1
    filterType.addItem("HP 24",   6);   // type 5
    filterType.addItem("Notch",   4);   // type 3
    filterType.addItem("Notch 24",11);  // type 10
    filterType.addItem("AP 12",   10);  // type 9
    filterType.addItem("Comb +",  9);   // type 8
    filterType.addItem("Comb -",  16);  // type 15
    filterType.addItem("Peak",    13);  // type 12
    filterType.addItem("Lo Shf",  14);  // type 13
    filterType.addItem("Hi Shf",  15);  // type 14
    filterType.setSelectedId(1, false);
    addAndMakeVisible(filterType);

    // Drive character dropdown — item IDs = driveChar+1; alphabetical order after None.
    driveChar.addItem("None",       1);
    driveChar.addItem("3-Band EQ",  7);
    driveChar.addItem("Bitcrusher", 5);
    driveChar.addItem("Clipper",    6);
    driveChar.addItem("Compressor", 8);
    driveChar.addItem("Fold",       4);
    driveChar.addItem("Hard Clip",  3);
    driveChar.addItem("Limiter",    9);
    driveChar.addItem("Ring Mod",  10);
    driveChar.addItem("Soft Clip",  2);
    driveChar.addItem("Tape Sat",  11);
    driveChar.setSelectedId(1, false);
    addAndMakeVisible(driveChar);

    // ─── Ranges ──────────────────────────────────────────────────────────
    pitchOctave.setRange(-4.0,   4.0,   1.0);   pitchOctave.setValue(0.0);
    pitchSemi  .setRange(-12.0, 12.0,   1.0);   pitchSemi  .setValue(0.0);
    pitchFine  .setRange(-100.0,100.0,  0.1);   pitchFine  .setValue(0.0);
    // ADSR: 0–100 display scale. 0→1 ms, 100→3 s (A/D/R); 0–100% (S). Mapping in adsrTime/adsrSus.
    // #217c: pitch A/D/R in seconds (0..10) with skew 0.3 — matches amp + filter envelopes.
    pitchAtk   .setRange(0.0, 10.0, 0.001);  pitchAtk.setValue(0.0);   pitchAtk.getSlider().setSkewFactor(0.3);
    pitchDec   .setRange(0.0, 10.0, 0.001);  pitchDec.setValue(0.03);  pitchDec.getSlider().setSkewFactor(0.3);
    pitchSus   .setRange(0.0, 100.0, 0.1);   pitchSus.setValue(0.0);
    pitchRel   .setRange(0.0, 10.0, 0.001);  pitchRel.setValue(0.03);  pitchRel.getSlider().setSkewFactor(0.3);
    pitchDepth .setRange(0.0,  24.0, 0.1);   pitchDepth.setValue(0.0);

    filterCutoff.setRange(20.0, 20000.0, 1.0);  filterCutoff.setValue(8000.0);
    filterCutoff.getSlider().setSkewFactorFromMidPoint(640.0);   // #216a: log feel — geo mean of 20..20000
    filterRes   .setRange(0.0,  100.0,  0.1);   filterRes   .setValue(20.0);
    // #217b: filter A/D/R in seconds (0..10) with skew 0.3 — matches amp envelope.
    filterAtk   .setRange(0.0,  10.0, 0.001);  filterAtk.setValue(0.03);  filterAtk.getSlider().setSkewFactor(0.3);
    filterDec   .setRange(0.0,  10.0, 0.001);  filterDec.setValue(0.09);  filterDec.getSlider().setSkewFactor(0.3);
    filterSus   .setRange(0.0, 100.0, 0.1);    filterSus.setValue(0.0);
    filterRel   .setRange(0.0,  10.0, 0.001);  filterRel.setValue(0.09);  filterRel.getSlider().setSkewFactor(0.3);
    filterDepth .setRange(0.0,   48.0,  0.1);   filterDepth.setValue(0.0);

    ampLevel  .setRange(0.0, 2.0,   0.01);  ampLevel  .setValue(0.5);  // Stage 19: −6 dB default
    ampSendEff.setRange(0.0, 1.0,  0.01);  ampSendEff.setValue(0.0);
    ampSendDly.setRange(0.0, 1.0,  0.01);  ampSendDly.setValue(0.0);
    ampSendRev.setRange(0.0, 1.0,  0.01);  ampSendRev.setValue(0.0);
    ampAccent .setRange(0.0, 12.0, 0.1);   ampAccent .setValue(0.0);
    // #217a: amp A/D/R now in seconds (0..10) with skew 0.3 so 100–200 ms sits at
    // knob centre. Sustain stays 0..100 %.
    ampAtk    .setRange(0.0, 10.0,  0.001); ampAtk .setValue(0.005); ampAtk.getSlider().setSkewFactor(0.3);
    ampDec    .setRange(0.0, 10.0,  0.001); ampDec .setValue(0.3);   ampDec.getSlider().setSkewFactor(0.3);
    ampSus    .setRange(0.0, 100.0, 0.1);   ampSus .setValue(80.0);
    ampRel    .setRange(0.0, 10.0,  0.001); ampRel .setValue(0.5);   ampRel.getSlider().setSkewFactor(0.3);

    // Insert knob ranges set dynamically by configureInsertAlgorithm(); defaults for Soft/Hard/Fold.
    driveDrive .setRange(0.0,   100.0, 0.1);   driveDrive .setValue(0.0);
    driveOutput.setRange(-24.0,   0.0, 0.1);   driveOutput.setValue(0.0);
    driveDither.setRange(0.0,   100.0, 0.1);   driveDither.setValue(0.0);
    driveTone  .setRange(20.0, 20000.0, 1.0);  driveTone  .setValue(20000.0);
    driveTone  .getSlider().setSkewFactorFromMidPoint(640.0);   // #216b: log feel default for full-range modes

    wireCallbacks();
}

void VoiceSection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

// Legacy ADSR (0-100 slider, max 3 s). Used by pitch + filter envelopes until
// they're migrated by #217b/c.
static juce::String formatAdsrTime(double v)
{
    double ms = std::max(1.0, v * 30.0);
    if (ms < 1000.0)
        return juce::String((int)std::round(ms)) + " ms";
    return juce::String(ms / 1000.0, 1) + " s";
}

static double parseAdsrTime(const juce::String& s)
{
    auto t = s.trim().toLowerCase();
    if (t.endsWith("ms"))
        return t.dropLastCharacters(2).trim().getDoubleValue() / 30.0;
    if (t.endsWith("s"))
        return t.dropLastCharacters(1).trim().getDoubleValue() * (100.0/3.0);
    // Bare number: assume milliseconds (120 → 120 ms → slider value 4.0)
    return t.getDoubleValue() / 30.0;
}

// #217a: seconds-domain formatter for the amp envelope. Slider value IS seconds (0..10).
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
    // Bare number: assume milliseconds (120 → 0.120 s)
    return t.getDoubleValue() / 1000.0;
}

void VoiceSection::wireCallbacks()
{
    // #217a (amp) + #217b (filter) + #217c (pitch): all ADSR time sliders take
    // seconds directly. Legacy formatAdsrTime / parseAdsrTime helpers are now
    // unused but kept in the file for one release in case any other slider is
    // discovered still depending on them; remove on the next pass.
    for (auto* k : { &ampAtk, &ampDec, &filterAtk, &filterDec, &filterRel,
                     &pitchAtk, &pitchDec, &pitchRel })
    {
        k->getSlider().textFromValueFunction = [](double v) { return formatAdsrTimeSec(v); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return parseAdsrTimeSec(s); };
    }
    // #217a: Amp Release at max (10 s) means "play to natural end".
    ampRel.getSlider().textFromValueFunction = [](double v) -> juce::String {
        if (v >= 10.0) return "End";
        return formatAdsrTimeSec(v);
    };
    ampRel.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
        if (s.trim().equalsIgnoreCase("end")) return 10.0;
        return parseAdsrTimeSec(s);
    };
    for (auto* k : { &pitchSus, &filterSus, &ampSus })
    {
        k->getSlider().textFromValueFunction = [](double v) -> juce::String {
            return juce::String((int)std::round(v)) + "%";
        };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
            return s.trim().dropLastCharacters(s.endsWith("%") ? 1 : 0).trim().getDoubleValue();
        };
    }

    // Filter cutoff Hz/kHz display
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

    // Status bar forwarding
    struct { KnobWithLabel* k; const char* name; } entries[] = {
        { &pitchOctave,  "Pitch Octave"            }, { &pitchSemi,    "Pitch Semitone"          },
        { &pitchFine,    "Pitch Fine"               }, { &pitchAtk,     "Pitch Attack"            },
        { &pitchDec,     "Pitch Decay"              }, { &pitchSus,     "Pitch Sustain"           },
        { &pitchRel,     "Pitch Release"            }, { &pitchDepth,   "Pitch Depth"             },
        { &filterCutoff, "Filter Cutoff"            }, { &filterRes,    "Filter Resonance"        },
        { &filterAtk,    "Filter Envelope Attack"   }, { &filterDec,    "Filter Envelope Decay"   },
        { &filterSus,    "Filter Envelope Sustain"  }, { &filterRel,    "Filter Envelope Release"  },
        { &filterDepth,  "Filter Envelope Depth"    }, { &ampLevel,     "Amp Level"               },
        { &ampSendEff,   "Amp Send Effect"          }, { &ampSendDly,   "Amp Send Delay"          },
        { &ampSendRev,   "Amp Send Reverb"          }, { &ampAccent,    "Amp Accent"               },
        { &ampAtk,       "Amp Attack"               }, { &ampDec,       "Amp Decay"               },
        { &ampSus,       "Amp Sustain"              }, { &ampRel,       "Amp Release"             },
    };
    for (auto& e : entries)
    {
        juce::String n(e.name);
        e.k->onStatusUpdate = [this, n](const juce::String&, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate(n, val);
        };
    }
    // Insert knobs: use their current label as the status bar name (changes per algorithm)
    for (auto* k : { &driveDrive, &driveOutput, &driveDither, &driveTone })
    {
        k->onStatusUpdate = [this, k](const juce::String& label, const juce::String& val) {
            if (onStatusUpdate) onStatusUpdate("Insert " + label, val);
        };
    }

    // Pitch config
    pitchOctave.onValueChanged = [this](double v) { apvtsSet("pitchOct",  (float)v); };
    pitchSemi  .onValueChanged = [this](double v) { apvtsSet("pitchSemi", (float)v); };
    pitchFine  .onValueChanged = [this](double v) { apvtsSet("pitchFine", (float)v); };

    // Pitch envelope
    pitchAtk  .onValueChanged = [this](double v) { apvtsSet("pEnvAtk", (float)v); };
    pitchDec  .onValueChanged = [this](double v) { apvtsSet("pEnvDec", (float)v); };
    pitchSus  .onValueChanged = [this](double v) { apvtsSet("pEnvSus", (float)v); };
    pitchRel  .onValueChanged = [this](double v) { apvtsSet("pEnvRel", (float)v); };
    pitchDepth.onValueChanged = [this](double v) { apvtsSet("pEnvDep", (float)v); };

    // Filter config — DropdownSelect uses 1-based IDs; filterType index = selectedId - 1.
    filterType.onChange = [this](int id) { apvtsSet("fltType", (float)(id - 1)); };
    filterCutoff.onValueChanged = [this](double v) { apvtsSet("fltCut", (float)v); };
    filterRes   .onValueChanged = [this](double v) { apvtsSet("fltRes", (float)(v / 100.0)); };

    // Filter envelope
    filterAtk  .onValueChanged = [this](double v) { apvtsSet("fEnvAtk", (float)v); };
    filterDec  .onValueChanged = [this](double v) { apvtsSet("fEnvDec", (float)v); };
    filterSus  .onValueChanged = [this](double v) { apvtsSet("fEnvSus", (float)v); };
    filterRel  .onValueChanged = [this](double v) { apvtsSet("fEnvRel", (float)v); };
    filterDepth.onValueChanged = [this](double v) { apvtsSet("fEnvDep", (float)v); };

    // Amp config
    ampLevel.onValueChanged = [this](double v) { apvtsSet("ampLvl", (float)v); };

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

    // Accent boost (Ring C coincidence)
    ampAccent.onValueChanged = [this](double v) { apvtsSet("accentDb", (float)v); };

    // Amp envelope
    ampAtk.onValueChanged = [this](double v) { apvtsSet("aEnvAtk", (float)v); };
    ampDec.onValueChanged = [this](double v) { apvtsSet("aEnvDec", (float)v); };
    ampSus.onValueChanged = [this](double v) { apvtsSet("aEnvSus", (float)v); };
    ampRel.onValueChanged = [this](double v) { apvtsSet("aEnvRel", (float)v); };

    // Insert algorithm — char is 0-based index; dropdown IDs are 1-based.
    // All insert knob callbacks are set in configureInsertAlgorithm().
    driveChar.onChange = [this](int id) {
        const int newChar = id - 1;
        // Reset params that change meaning across algorithms to prevent jarring transitions (#147).
        // Check current algorithm BEFORE writing drvChar so we can detect the EQ→other direction.
        const int oldChar = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                            ? proc.getRhythm(rhythmIndex).voiceParams.driveChar : -1;
        if (newChar == 6)
        {
            // Entering EQ: force 0 dB neutral on all bands (drvDrv/drvDit stored as 50/100=0 dB).
            apvtsSet("drvDrv",    50.0f);
            apvtsSet("drvDit",    50.0f);
            apvtsSet("eqMidGain", 0.0f);
        }
        else if (oldChar == 6)
        {
            // Leaving EQ: reset shared params to neutral drive defaults.
            apvtsSet("drvDrv", 50.0f);
            apvtsSet("drvDit", 50.0f);
        }
        apvtsSet("drvChar", (float)newChar);
        configureInsertAlgorithm(newChar);
    };
}

void VoiceSection::setRhythm(int ri)
{
    rhythmIndex = ri;
    loadFromRhythm();
    refreshModulatedIndicators();
}

void VoiceSection::refreshModulatedIndicators()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& assigns = proc.getRhythm(rhythmIndex).modulationMatrix.getAssignments();

    // Build a quick lookup of which destination IDs are currently assigned.
    auto isAssigned = [&assigns](const char* destId) -> bool
    {
        const std::string s = destId;
        for (const auto& a : assigns)
            if (a.destinationId == s) return true;
        return false;
    };

    // #218: single Pitch destination — live arc shown only on the Semi knob.
    pitchOctave .setIsModulated(false);
    pitchSemi   .setIsModulated(isAssigned("pitch.semitones"));
    pitchFine   .setIsModulated(false);
    filterCutoff.setIsModulated(isAssigned("filter.cutoff"));
    filterRes   .setIsModulated(isAssigned("filter.resonance"));
    filterAtk   .setIsModulated(isAssigned("fenv.attack"));
    filterDec   .setIsModulated(isAssigned("fenv.decay"));
    filterDepth .setIsModulated(isAssigned("fenv.depth"));
    ampAtk      .setIsModulated(isAssigned("amp.attack"));
    ampDec      .setIsModulated(isAssigned("amp.decay"));
    ampSus      .setIsModulated(isAssigned("amp.sustain"));
    ampRel      .setIsModulated(isAssigned("amp.release"));
    // #223 new destinations
    pitchDepth  .setIsModulated(isAssigned("pitch.envDepth"));
    ampLevel    .setIsModulated(isAssigned("amp.level"));
    ampAccent   .setIsModulated(isAssigned("accentDb"));
    // Insert knobs map differently per algorithm; tag both possible meanings.
    driveDrive  .setIsModulated(isAssigned("insert.drive") || isAssigned("insert.bits"));
    driveOutput .setIsModulated(isAssigned("insert.output") || isAssigned("insert.rate"));
    driveDither .setIsModulated(isAssigned("insert.dither"));
    driveTone   .setIsModulated(isAssigned("insert.lpf"));

    // Live-arc: read atomic snapshot written by audio thread (#133).
    const auto& snap = proc.modSnapshot[rhythmIndex];
    auto sn  = [&](int i) { return snap[i].get(); };
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    auto arc = [&](bool assigned, int idx) -> float { return assigned ? sn(idx) : kNaN; };

    using P = PluginProcessor;
    // #218: pitch.octave / pitch.fine no longer modulatable; only the Semi knob shows live arc.
    pitchOctave .setModulatedNorm(kNaN);
    pitchSemi   .setModulatedNorm(arc(isAssigned("pitch.semitones"),  P::kSnapPitchSemi));
    pitchFine   .setModulatedNorm(kNaN);
    filterCutoff.setModulatedNorm(arc(isAssigned("filter.cutoff"),    P::kSnapFilterCutoff));
    filterRes   .setModulatedNorm(arc(isAssigned("filter.resonance"), P::kSnapFilterRes));
    filterAtk   .setModulatedNorm(arc(isAssigned("fenv.attack"),      P::kSnapFenvAtk));
    filterDec   .setModulatedNorm(arc(isAssigned("fenv.decay"),       P::kSnapFenvDec));
    filterDepth .setModulatedNorm(arc(isAssigned("fenv.depth"),       P::kSnapFenvDepth));
    ampAtk      .setModulatedNorm(arc(isAssigned("amp.attack"),       P::kSnapAmpAtk));
    ampDec      .setModulatedNorm(arc(isAssigned("amp.decay"),        P::kSnapAmpDec));
    ampSus      .setModulatedNorm(arc(isAssigned("amp.sustain"),      P::kSnapAmpSus));
    ampRel      .setModulatedNorm(arc(isAssigned("amp.release"),      P::kSnapAmpRel));
    driveDrive  .setModulatedNorm(isAssigned("insert.drive") ? sn(P::kSnapInsDrive)
                                : isAssigned("insert.bits")  ? sn(P::kSnapInsBits) : kNaN);
    driveOutput .setModulatedNorm(arc(isAssigned("insert.output"),    P::kSnapInsOutput));
    driveDither .setModulatedNorm(arc(isAssigned("insert.dither"),    P::kSnapInsDither));
    driveTone   .setModulatedNorm(arc(isAssigned("insert.lpf"),       P::kSnapInsLpf));
    // #223 new destinations
    pitchDepth  .setModulatedNorm(arc(isAssigned("pitch.envDepth"),   P::kSnapPitchEnvDep));
    ampLevel    .setModulatedNorm(arc(isAssigned("amp.level"),        P::kSnapAmpLvl));
    ampAccent   .setModulatedNorm(arc(isAssigned("accentDb"),         P::kSnapAccent));
}

void VoiceSection::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const Rhythm& r = proc.getRhythm(rhythmIndex);
    const auto& p = r.voiceParams;

    pitchOctave .setValue(p.pitchOctave,         juce::dontSendNotification);
    pitchSemi   .setValue(p.pitchSemitones,      juce::dontSendNotification);
    pitchFine   .setValue(p.pitchFine,           juce::dontSendNotification);
    // #217c: pitch envelope times now in seconds directly.
    pitchAtk    .setValue(p.pitchEnvAtk,         juce::dontSendNotification);
    pitchDec    .setValue(p.pitchEnvDec,         juce::dontSendNotification);
    pitchSus    .setValue(p.pitchEnvSus * 100.0, juce::dontSendNotification);
    pitchRel    .setValue(p.pitchEnvRel,         juce::dontSendNotification);
    pitchDepth  .setValue(p.pitchEnvDepth,       juce::dontSendNotification);

    filterType.setSelectedId(p.filterType + 1, false);
    filterCutoff.setValue(p.filterCutoff,           juce::dontSendNotification);
    filterRes   .setValue(p.filterRes * 100.0,      juce::dontSendNotification);
    // #217b: filter envelope times now in seconds directly.
    filterAtk   .setValue(p.filterEnvAtk,         juce::dontSendNotification);
    filterDec   .setValue(p.filterEnvDec,         juce::dontSendNotification);
    filterSus   .setValue(p.filterEnvSus  * 100.0, juce::dontSendNotification);
    filterRel   .setValue(p.filterEnvRel,         juce::dontSendNotification);
    filterDepth .setValue(p.filterEnvDepth,         juce::dontSendNotification);

    ampLevel.setValue(p.ampLevel,               juce::dontSendNotification);

    {
        const auto chPfx = "ch" + juce::String(rhythmIndex) + "_";
        auto load = [&](KnobWithLabel& k, const char* param) {
            if (auto* raw = proc.apvts.getRawParameterValue(chPfx + param))
                k.setValue(*raw, juce::dontSendNotification);
        };
        load(ampSendEff, "sendEff");
        load(ampSendDly, "sendDly");
        load(ampSendRev, "sendRev");
    }

    ampAccent.setValue(p.accentDb,               juce::dontSendNotification);
    // #217a: amp slider value IS seconds now (no conversion); Sustain stays 0..100 %.
    ampAtk  .setValue(p.ampEnvAtk,                                  juce::dontSendNotification);
    ampDec  .setValue(p.ampEnvDec,                                  juce::dontSendNotification);
    ampSus  .setValue(p.ampEnvSus  * 100.0,                         juce::dontSendNotification);
    ampRel  .setValue(p.ampRelToEnd ? 10.0 : p.ampEnvRel,            juce::dontSendNotification);

    driveChar.setSelectedId(p.driveChar + 1, false);
    configureInsertAlgorithm(p.driveChar);  // sets all insert knob ranges/labels/values/callbacks
}

static juce::String fmtHz(double v)
{
    return v < 1000.0 ? juce::String((int)std::round(v)) + " Hz"
                      : juce::String(v / 1000.0, 1) + " kHz";
}
static double parseHz(const juce::String& s)
{
    auto t = s.trim().toLowerCase();
    if (t.endsWith("khz")) return t.dropLastCharacters(3).trim().getDoubleValue() * 1000.0;
    if (t.endsWith("hz"))  return t.dropLastCharacters(2).trim().getDoubleValue();
    return t.getDoubleValue();
}

void VoiceSection::configureInsertAlgorithm(int charId)
{
    // Null all insert callbacks first — prevents spurious APVTS writes during range changes.
    for (auto* k : { &driveDrive, &driveOutput, &driveDither, &driveTone })
        k->onValueChanged = nullptr;

    const VoiceParams* p = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                           ? &proc.getRhythm(rhythmIndex).voiceParams : nullptr;

    switch (charId)
    {
        case 0:  // ── None — hide all insert knobs ──────────────────────────
            driveDrive .setVisible(false);
            driveOutput.setVisible(false);
            driveDither.setVisible(false);
            driveTone  .setVisible(false);
            break;

        case 1: case 2: case 3:  // ── Soft Clip / Hard Clip / Fold ─────────
            driveDrive.setLabel("Drive");
            driveDrive.setRange(0.0, 100.0, 0.1);
            driveDrive.getSlider().textFromValueFunction = nullptr;
            driveDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) driveDrive.setValue(p->driveDrive, juce::dontSendNotification);
            driveDrive.setVisible(true);

            driveOutput.setLabel("Output");
            driveOutput.setRange(-24.0, 0.0, 0.1);
            driveOutput.getSlider().textFromValueFunction = nullptr;
            driveOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) driveOutput.setValue(p->driveOutput, juce::dontSendNotification);
            driveOutput.setVisible(true);

            driveDither.setVisible(false);

            driveTone.setLabel("LPF");
            driveTone.setRange(20.0, 20000.0, 1.0);
            driveTone.getSlider().setSkewFactorFromMidPoint(640.0);   // #216b
            driveTone.getSlider().textFromValueFunction = fmtHz;
            driveTone.getSlider().valueFromTextFunction = parseHz;
            if (p) driveTone.setValue(p->driveTone, juce::dontSendNotification);
            driveTone.setVisible(true);

            driveDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            driveOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            driveTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        case 4:  // ── Bitcrusher ───────────────────────────────────────────
            driveDrive.setLabel("Bits");
            driveDrive.setRange(1.0, 16.0, 1.0);
            driveDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)v) + " bits";
            };
            driveDrive.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return juce::jlimit(1.0, 16.0, s.retainCharacters("0123456789").getDoubleValue());
            };
            if (p) driveDrive.setValue(p->drvBits, juce::dontSendNotification);
            driveDrive.setVisible(true);

            driveOutput.setLabel("Rate");
            driveOutput.setRange(100.0, 48000.0, 1.0);
            driveOutput.getSlider().setSkewFactorFromMidPoint(2190.0); // log feel: geometric mean of range
            driveOutput.getSlider().textFromValueFunction = fmtHz;
            driveOutput.getSlider().valueFromTextFunction = parseHz;
            if (p) driveOutput.setValue(p->driveRate, juce::dontSendNotification);
            driveOutput.setVisible(true);

            driveDither.setLabel("Dither");
            driveDither.setRange(0.0, 100.0, 0.1);
            driveDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            driveDither.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return s.retainCharacters("0123456789.").getDoubleValue();
            };
            if (p) driveDither.setValue(p->drvDither, juce::dontSendNotification);
            driveDither.setVisible(true);

            driveTone.setLabel("LPF");
            driveTone.setRange(20.0, 20000.0, 1.0);
            driveTone.getSlider().setSkewFactorFromMidPoint(640.0);   // #216b
            driveTone.getSlider().textFromValueFunction = fmtHz;
            driveTone.getSlider().valueFromTextFunction = parseHz;
            if (p) driveTone.setValue(p->driveTone, juce::dontSendNotification);
            driveTone.setVisible(true);

            driveDrive .onValueChanged = [this](double v) { apvtsSet("drvBits", (float)v); };
            driveOutput.onValueChanged = [this](double v) { apvtsSet("drvRate", (float)v); };
            driveDither.onValueChanged = [this](double v) { apvtsSet("drvDit",  (float)v); };
            driveTone  .onValueChanged = [this](double v) { apvtsSet("drvTon",  (float)v); };
            break;

        case 5:  // ── Clipper — threshold + output, post-LPF ──────────────────
            driveDrive.setLabel("Threshold");
            driveDrive.setRange(0.0, 100.0, 0.1);
            driveDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                // Display as "ceiling %" — at 100 the clipper is open, at 0 fully clamped.
                return juce::String((int)std::round(v)) + "%";
            };
            driveDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) driveDrive.setValue(p->driveDrive, juce::dontSendNotification);
            driveDrive.setVisible(true);

            driveOutput.setLabel("Output");
            driveOutput.setRange(-24.0, 0.0, 0.1);
            driveOutput.getSlider().textFromValueFunction = nullptr;
            driveOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) driveOutput.setValue(p->driveOutput, juce::dontSendNotification);
            driveOutput.setVisible(true);

            driveDither.setVisible(false);

            driveTone.setLabel("LPF");
            driveTone.setRange(20.0, 20000.0, 1.0);
            driveTone.getSlider().setSkewFactorFromMidPoint(640.0);   // #216b
            driveTone.getSlider().textFromValueFunction = fmtHz;
            driveTone.getSlider().valueFromTextFunction = parseHz;
            if (p) driveTone.setValue(p->driveTone, juce::dontSendNotification);
            driveTone.setVisible(true);

            driveDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            driveOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            driveTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        case 6:  // ── 3-Band EQ: Low shelf / Mid peak / High shelf ──────────
        {
            auto dbFmt = [](double v) -> juce::String {
                return (v >= 0.0 ? "+" : "") + juce::String(v, 1) + " dB";
            };
            auto dbParse = [](const juce::String& s) -> double {
                return s.retainCharacters("0123456789.-+").getDoubleValue();
            };

            driveDrive.setLabel("Low");
            driveDrive.setRange(-18.0, 18.0, 0.1);
            driveDrive.getSlider().textFromValueFunction = dbFmt;
            driveDrive.getSlider().valueFromTextFunction = dbParse;
            // driveDrive stores low gain as 0..100 (50 = 0 dB): load back
            if (p) driveDrive.setValue(p->driveDrive / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            else   driveDrive.setValue(0.0, juce::dontSendNotification);
            driveDrive.setVisible(true);

            driveOutput.setLabel("High");
            driveOutput.setRange(-18.0, 18.0, 0.1);
            driveOutput.getSlider().textFromValueFunction = dbFmt;
            driveOutput.getSlider().valueFromTextFunction = dbParse;
            // high gain stored in drvDither (0..100): load back
            if (p) driveOutput.setValue(p->drvDither / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            else   driveOutput.setValue(0.0, juce::dontSendNotification);
            driveOutput.setVisible(true);

            driveDither.setLabel("Mid");
            driveDither.setRange(-18.0, 18.0, 0.1);
            driveDither.getSlider().textFromValueFunction = dbFmt;
            driveDither.getSlider().valueFromTextFunction = dbParse;
            if (p) driveDither.setValue(p->eqMidGain, juce::dontSendNotification);
            else   driveDither.setValue(0.0, juce::dontSendNotification);
            driveDither.setVisible(true);

            driveTone.setLabel("Mid Hz");
            driveTone.getSlider().setSkewFactor(1.0);  // reset any log skew from Compressor mode
            driveTone.setRange(200.0, 8000.0, 1.0);
            driveTone.getSlider().textFromValueFunction = fmtHz;
            driveTone.getSlider().valueFromTextFunction = parseHz;
            if (p) driveTone.setValue(juce::jlimit(200.0, 8000.0, (double)p->driveTone), juce::dontSendNotification);
            else   driveTone.setValue(1000.0, juce::dontSendNotification);
            driveTone.setVisible(true);

            // Low gain -18..18 → stored as 0..100 in drvDrv
            driveDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)((v + 18.0) / 36.0 * 100.0)); };
            // High gain -18..18 → stored as 0..100 in drvDit (reused slot)
            driveOutput.onValueChanged = [this](double v) { apvtsSet("drvDit", (float)((v + 18.0) / 36.0 * 100.0)); };
            // Mid gain → direct eqMidGain param
            driveDither.onValueChanged = [this](double v) { apvtsSet("eqMidGain", (float)v); };
            // Mid frequency → drvTon
            driveTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;
        }

        case 7: case 8:  // ── Compressor / Limiter ──────────────────────────
        {
            driveDrive.setLabel(charId == 8 ? "Ceiling" : "Threshold");
            driveDrive.setRange(0.0, 100.0, 0.1);
            driveDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return "-" + juce::String((int)std::round(v * 0.4)) + " dB";
            };
            driveDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) driveDrive.setValue(p->driveDrive, juce::dontSendNotification);
            else   driveDrive.setValue(50.0, juce::dontSendNotification);
            driveDrive.setVisible(true);

            driveOutput.setLabel("Output");
            driveOutput.setRange(-24.0, 0.0, 0.1);
            driveOutput.getSlider().textFromValueFunction = nullptr;
            driveOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) driveOutput.setValue(p->driveOutput, juce::dontSendNotification);
            else   driveOutput.setValue(0.0, juce::dontSendNotification);
            driveOutput.setVisible(true);

            driveDither.setLabel("Attack");
            driveDither.setRange(0.0, 100.0, 0.1);
            driveDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v * 2.0)) + " ms";
            };
            driveDither.getSlider().valueFromTextFunction = nullptr;
            if (p) driveDither.setValue(p->drvDither, juce::dontSendNotification);
            else   driveDither.setValue(5.0, juce::dontSendNotification);
            driveDither.setVisible(true);

            driveTone.setLabel("Release");
            driveTone.setRange(20.0, 2000.0, 1.0);
            driveTone.getSlider().setSkewFactorFromMidPoint(200.0);
            driveTone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v < 1000.0 ? juce::String((int)v) + " ms"
                                  : juce::String(v / 1000.0, 2) + " s";
            };
            driveTone.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                auto t = s.trim().toLowerCase();
                if (t.endsWith("ms")) return t.dropLastCharacters(2).trim().getDoubleValue();
                if (t.endsWith("s"))  return t.dropLastCharacters(1).trim().getDoubleValue() * 1000.0;
                return t.getDoubleValue();
            };
            if (p) driveTone.setValue(juce::jlimit(20.0, 2000.0, (double)p->driveTone), juce::dontSendNotification);
            else   driveTone.setValue(100.0, juce::dontSendNotification);
            driveTone.setVisible(true);

            driveDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            driveOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            driveDither.onValueChanged = [this](double v) { apvtsSet("drvDit", (float)v); };
            driveTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;
        }

        case 9:  // ── Ring Modulator — Mix + Freq ───────────────────────────
            driveDrive.setLabel("Mix");
            driveDrive.setRange(0.0, 100.0, 0.1);
            driveDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            driveDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) driveDrive.setValue(p->driveDrive, juce::dontSendNotification);
            else   driveDrive.setValue(50.0, juce::dontSendNotification);
            driveDrive.setVisible(true);

            driveOutput.setVisible(false);
            driveDither.setVisible(false);

            driveTone.setLabel("Freq");
            driveTone.getSlider().setSkewFactorFromMidPoint(223.6);
            driveTone.setRange(10.0, 5000.0, 1.0);
            driveTone.getSlider().textFromValueFunction = fmtHz;
            driveTone.getSlider().valueFromTextFunction = parseHz;
            if (p) driveTone.setValue(juce::jlimit(10.0, 5000.0, (double)p->driveTone), juce::dontSendNotification);
            else   driveTone.setValue(440.0, juce::dontSendNotification);
            driveTone.setVisible(true);

            driveDrive.onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            driveTone .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        case 10:  // ── Tape Saturation — Drive / Output / Tone ─────────────
            driveDrive.setLabel("Drive");
            driveDrive.setRange(0.0, 100.0, 0.1);
            driveDrive.getSlider().textFromValueFunction = nullptr;
            driveDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) driveDrive.setValue(p->driveDrive, juce::dontSendNotification);
            else   driveDrive.setValue(0.0, juce::dontSendNotification);
            driveDrive.setVisible(true);

            driveOutput.setLabel("Output");
            driveOutput.setRange(-24.0, 0.0, 0.1);
            driveOutput.getSlider().textFromValueFunction = nullptr;
            driveOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) driveOutput.setValue(p->driveOutput, juce::dontSendNotification);
            else   driveOutput.setValue(0.0, juce::dontSendNotification);
            driveOutput.setVisible(true);

            driveDither.setVisible(false);

            driveTone.setLabel("Tone");
            driveTone.getSlider().setSkewFactor(1.0);
            driveTone.getSlider().setSkewFactorFromMidPoint(2000.0);
            driveTone.setRange(200.0, 20000.0, 1.0);
            driveTone.getSlider().textFromValueFunction = fmtHz;
            driveTone.getSlider().valueFromTextFunction = parseHz;
            if (p) driveTone.setValue(juce::jlimit(200.0, 20000.0, (double)p->driveTone), juce::dontSendNotification);
            else   driveTone.setValue(10000.0, juce::dontSendNotification);
            driveTone.setVisible(true);

            driveDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            driveOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            driveTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        default: break;
    }

    // Refresh display text and reposition knobs
    for (auto* k : { &driveDrive, &driveOutput, &driveDither, &driveTone })
    { k->getSlider().updateText(); k->repaint(); }

    resized();

    if (onInsertAlgorithmChanged) onInsertAlgorithmChanged(charId);
}

// ─── Layout ──────────────────────────────────────────────────────────────────

void VoiceSection::resized()
{
    const int w      = getWidth();
    const int h      = getHeight();
    const int divW   = 6;
    const int labelH = 14;
    const int gap    = 4;
    const int rowH   = (h - labelH - gap) / 2;

    const int kW = (w - 3 * divW) / 19;   // 19 cols, 3 dividers

    const int pitchX  = 0;
    const int filterX = 5 * kW + divW;
    const int ampX    = 10 * kW + 2 * divW;
    const int driveX  = 15 * kW + 3 * divW;

    const int row1Y = labelH;
    const int row2Y = labelH + rowH + gap;

    // ─── Pitch ───────────────────────────────────────────────────────────
    pitchOctave.setBounds(pitchX + 0 * kW, row1Y, kW, rowH);
    pitchSemi  .setBounds(pitchX + 1 * kW, row1Y, kW, rowH);
    pitchFine  .setBounds(pitchX + 2 * kW, row1Y, kW, rowH);
    pitchDepth .setBounds(pitchX + 3 * kW, row1Y, kW, rowH);  // depth on config row

    pitchAtk.setBounds(pitchX + 0 * kW, row2Y, kW, rowH);
    pitchDec.setBounds(pitchX + 1 * kW, row2Y, kW, rowH);
    pitchSus.setBounds(pitchX + 2 * kW, row2Y, kW, rowH);
    pitchRel.setBounds(pitchX + 3 * kW, row2Y, kW, rowH);

    // ─── Filter ──────────────────────────────────────────────────────────
    // Dropdown half-height centred; Cutoff, Res, Depth on cols 1–3.
    filterType  .setBounds(filterX + 0 * kW, row1Y + rowH / 4, kW, rowH / 2);
    filterCutoff.setBounds(filterX + 1 * kW, row1Y, kW, rowH);
    filterRes   .setBounds(filterX + 2 * kW, row1Y, kW, rowH);
    filterDepth .setBounds(filterX + 3 * kW, row1Y, kW, rowH);  // depth on config row

    filterAtk.setBounds(filterX + 0 * kW, row2Y, kW, rowH);
    filterDec.setBounds(filterX + 1 * kW, row2Y, kW, rowH);
    filterSus.setBounds(filterX + 2 * kW, row2Y, kW, rowH);
    filterRel.setBounds(filterX + 3 * kW, row2Y, kW, rowH);

    // ─── Amp ─────────────────────────────────────────────────────────────
    ampLevel  .setBounds(ampX + 0 * kW, row1Y, kW, rowH);
    ampSendEff.setBounds(ampX + 1 * kW, row1Y, kW, rowH);
    ampSendDly.setBounds(ampX + 2 * kW, row1Y, kW, rowH);
    ampSendRev.setBounds(ampX + 3 * kW, row1Y, kW, rowH);
    ampAccent .setBounds(ampX + 4 * kW, row1Y, kW, rowH);

    ampAtk.setBounds(ampX + 0 * kW, row2Y, kW, rowH);
    ampDec.setBounds(ampX + 1 * kW, row2Y, kW, rowH);
    ampSus.setBounds(ampX + 2 * kW, row2Y, kW, rowH);
    ampRel.setBounds(ampX + 3 * kW, row2Y, kW, rowH);

    // ─── Insert ──────────────────────────────────────────────────────────
    // Dropdown on row 1; knob layout depends on algorithm (Bitcrusher uses all 4 slots).
    driveChar.setBounds(driveX, row1Y + rowH / 4, 4 * kW, rowH / 2);
    // Bitcrusher (id=5), EQ (id=7), Compressor (id=8), Limiter (id=9) use 4 knob slots.
    // Ring Mod (id=10) and Tape Sat (id=11) use 2 and 3 slots respectively.
    const int selId = driveChar.getSelectedId();
    const bool showFour = (selId == 5 || (selId >= 7 && selId <= 9));
    driveDrive .setBounds(driveX + 0 * kW, row2Y, kW, rowH);
    driveOutput.setBounds(driveX + 1 * kW, row2Y, kW, rowH);
    if (showFour)
    {
        driveDither.setBounds(driveX + 2 * kW, row2Y, kW, rowH);
        driveTone  .setBounds(driveX + 3 * kW, row2Y, kW, rowH);
    }
    else
    {
        driveTone  .setBounds(driveX + 2 * kW, row2Y, kW, rowH);
        driveDither.setBounds(driveX + 3 * kW, row2Y, kW, rowH);  // hidden, off-screen position
    }
}

void VoiceSection::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w      = getWidth();
    const int h      = getHeight();
    const int divW   = 6;
    const int labelH = 14;
    const int kW     = (w - 3 * divW) / 19;

    g.setColour(MuClidLookAndFeel::colour(Id::segmentInactiveBorder));
    const float div1X = static_cast<float>(5 * kW) + divW * 0.5f;
    const float div2X = static_cast<float>(10 * kW + divW) + divW * 0.5f;
    const float div3X = static_cast<float>(15 * kW + 2 * divW) + divW * 0.5f;
    g.drawLine(div1X, h * 0.05f, div1X, h * 0.95f, 0.5f);
    g.drawLine(div2X, h * 0.05f, div2X, h * 0.95f, 0.5f);
    g.drawLine(div3X, h * 0.05f, div3X, h * 0.95f, 0.5f);

    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("PITCH",  0,                      0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("FILTER", 5 * kW + divW,          0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("AMP",    10 * kW + 2 * divW,     0, 5 * kW, labelH, juce::Justification::centred, false);
    g.drawText("INSERT", 15 * kW + 3 * divW,     0, 4 * kW, labelH, juce::Justification::centred, false);
}
