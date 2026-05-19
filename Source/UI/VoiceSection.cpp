#include "VoiceSection.h"
#include "../PluginProcessor.h"

// #435: per-algorithm default table moved to UI/InsertAlgoDefaults.h
// (mu_ui::kInsertAlgoDefaults). Both this page and MixerChannel_Insert.cpp
// now share that single source of truth; previous local copies had drifted.

VoiceSection::VoiceSection(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &pitchOctave, &pitchSemi, &pitchFine,
                     &pitchAtk, &pitchDec, &pitchSus, &pitchRel, &pitchDepth,
                     &filterCutoff, &filterRes,
                     &filterAtk, &filterDec, &filterSus, &filterRel, &filterDepth,
                     &ampLevel, &ampSendEff, &ampSendDly, &ampSendRev, &ampAccent,
                     &ampAtk, &ampDec, &ampSus, &ampRel,
                     &insertDrive, &insertOutput, &insertDither, &insertTone })
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

    // Drive character dropdown — item IDs = insertAlgo+1; alphabetical order after None.
    insertAlgo.addItem("None",        1);
    insertAlgo.addItem("3-Band EQ",   7);
    insertAlgo.addItem("Bitcrusher",  5);
    insertAlgo.addItem("Clipper",     6);
    insertAlgo.addItem("Compressor",  8);
    insertAlgo.addItem("Fold",        4);
    insertAlgo.addItem("Hard Clip",   3);
    insertAlgo.addItem("Karplus",    12);   // #422
    insertAlgo.addItem("Limiter",     9);
    insertAlgo.addItem("Ring Mod",   10);
    insertAlgo.addItem("Soft Clip",   2);
    insertAlgo.addItem("Tape Sat",   11);
    insertAlgo.addItem("Vocoder",    13);   // #423
    insertAlgo.setSelectedId(1, false);
    addAndMakeVisible(insertAlgo);

    // ─── Ranges ──────────────────────────────────────────────────────────
    pitchOctave.setRange(-4.0,   4.0,   1.0);   pitchOctave.setValue(0.0);
    pitchSemi  .setRange(-12.0, 12.0,   1.0);   pitchSemi  .setValue(0.0);
    pitchFine  .setRange(-100.0,100.0,  0.1);   pitchFine  .setValue(0.0);
    // ADSR: 0–100 display scale. 0→1 ms, 100→3 s (A/D/R); 0–100% (S). Mapping in adsrTime/adsrSus.
    // #287: pitch A/D/R in seconds (0..10) with skew 0.3 — matches amp + filter envelopes.
    pitchAtk   .setRange(0.0, 10.0, 0.001);  pitchAtk.setValue(0.0);   pitchAtk.getSlider().setSkewFactor(0.3);
    pitchDec   .setRange(0.0, 10.0, 0.001);  pitchDec.setValue(0.03);  pitchDec.getSlider().setSkewFactor(0.3);
    pitchSus   .setRange(0.0, 100.0, 0.1);   pitchSus.setValue(0.0);
    pitchRel   .setRange(0.0, 10.0, 0.001);  pitchRel.setValue(0.03);  pitchRel.getSlider().setSkewFactor(0.3);
    pitchDepth .setRange(0.0,  24.0, 0.1);   pitchDepth.setValue(0.0);

    filterCutoff.setRange(20.0, 20000.0, 1.0);  filterCutoff.setValue(8000.0);
    filterCutoff.getSlider().setSkewFactorFromMidPoint(640.0);   // #216: log feel — geo mean of 20..20000
    filterRes   .setRange(0.0,  100.0,  0.1);   filterRes   .setValue(20.0);
    // #286: filter A/D/R in seconds (0..10) with skew 0.3 — matches amp envelope.
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
    // #217: amp A/D/R now in seconds (0..10) with skew 0.3 so 100–200 ms sits at
    // knob centre. Sustain stays 0..100 %.
    ampAtk    .setRange(0.0, 10.0,  0.001); ampAtk .setValue(0.005); ampAtk.getSlider().setSkewFactor(0.3);
    ampDec    .setRange(0.0, 10.0,  0.001); ampDec .setValue(0.3);   ampDec.getSlider().setSkewFactor(0.3);
    ampSus    .setRange(0.0, 100.0, 0.1);   ampSus .setValue(80.0);
    ampRel    .setRange(0.0, 10.0,  0.001); ampRel .setValue(0.5);   ampRel.getSlider().setSkewFactor(0.3);

    // Insert knob ranges set dynamically by configureInsertAlgorithm(); defaults for Soft/Hard/Fold.
    insertDrive .setRange(0.0,   100.0, 0.1);   insertDrive .setValue(0.0);
    insertOutput.setRange(-24.0,   0.0, 0.1);   insertOutput.setValue(0.0);
    insertDither.setRange(0.0,   100.0, 0.1);   insertDither.setValue(0.0);
    insertTone  .setRange(20.0, 20000.0, 1.0);  insertTone  .setValue(20000.0);
    insertTone  .getSlider().setSkewFactorFromMidPoint(640.0);   // #289: log feel default for full-range modes

    wireCallbacks();
}

void VoiceSection::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
        p->setValueNotifyingHost(p->convertTo0to1(v));
}

// #217: seconds-domain formatter for the amp envelope. Slider value IS seconds (0..10).
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
    // #217 (amp) + #286 (filter) + #287 (pitch): all ADSR time sliders take seconds directly.
    for (auto* k : { &ampAtk, &ampDec, &filterAtk, &filterDec, &filterRel,
                     &pitchAtk, &pitchDec, &pitchRel })
    {
        k->getSlider().textFromValueFunction = [](double v) { return formatAdsrTimeSec(v); };
        k->getSlider().valueFromTextFunction = [](const juce::String& s) { return parseAdsrTimeSec(s); };
    }
    // #217: Amp Release at max (10 s) means "play to natural end".
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
    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
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
    filterType.onChange = [this](int id) {
        apvtsSet("fltType", (float)(id - 1));
        // #379: status-bar feedback when filter type changes — show the dropdown item text.
        if (onStatusUpdate) onStatusUpdate("Filter Type", filterType.getText());
    };
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
    insertAlgo.onChange = [this](int id) {
        const int newChar = id - 1;
        const int oldChar = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                            ? proc.getRhythm(rhythmIndex).voiceParams.insertAlgo : -1;

        // Save the outgoing algorithm's current params into its snapshot.
        if (oldChar >= 0 && oldChar <= 10 && rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
        {
            const auto& p           = proc.getRhythm(rhythmIndex).voiceParams;
            auto&       snap        = insertSnapshots[oldChar];
            snap.insertDrive         = p.insertDrive;
            snap.insertOutput        = p.insertOutput;
            snap.insertDither          = p.insertDither;
            snap.insertTone          = p.insertTone;
            snap.insertEqMid          = p.insertEqMid;
            snap.insertBits            = p.insertBits;
            snap.insertRate          = p.insertRate;
            insertSnapshotValid[oldChar] = true;
        }

        // Restore the incoming algorithm from its saved snapshot, or first-visit defaults.
        // #435: defaults are now shared with MixerChannel_Insert via UI/InsertAlgoDefaults.h.
        const InsertAlgoSnapshot& snap = insertSnapshotValid[newChar]
                                        ? insertSnapshots[newChar]
                                        : mu_ui::kInsertAlgoDefaults[newChar];
        apvtsSet("drvDrv",    snap.insertDrive);
        apvtsSet("drvOut",    snap.insertOutput);
        apvtsSet("drvDit",    snap.insertDither);
        apvtsSet("drvTon",    snap.insertTone);
        apvtsSet("eqMidGain", snap.insertEqMid);
        apvtsSet("drvBits",   snap.insertBits);
        apvtsSet("drvRate",   snap.insertRate);
        apvtsSet("drvChar",   (float)newChar);
        configureInsertAlgorithm(newChar);
        // #379: status-bar feedback when Insert algorithm changes.
        if (onStatusUpdate) onStatusUpdate("Insert Algorithm", insertAlgo.getText());
    };
}

void VoiceSection::setRhythm(int ri)
{
    rhythmIndex = ri;
    std::fill(std::begin(insertSnapshotValid), std::end(insertSnapshotValid), false);
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
        for (const auto& a : assigns)
            if (a.destinationId == destId) return true;
        return false;
    };

    // Only show mod indicators (ring + live arc) while playing — when stopped the
    // snapshot holds the last played position, which would be a misleading permanent indicator.
    const auto& snap    = proc.modSnapshot[rhythmIndex];
    auto sn  = [&](int i) { return snap[i].load(); };
    const float kNaN    = std::numeric_limits<float>::quiet_NaN();
    const bool  playing = proc.sequencerPlaying.load();

    // #218: single Pitch destination — ring shown only on the Semi knob.
    pitchOctave .setIsModulated(false);
    pitchSemi   .setIsModulated(playing && isAssigned("pitch.semitones"));
    pitchFine   .setIsModulated(false);
    filterCutoff.setIsModulated(playing && isAssigned("filter.cutoff"));
    filterRes   .setIsModulated(playing && isAssigned("filter.resonance"));
    filterAtk   .setIsModulated(playing && isAssigned("fenv.attack"));
    filterDec   .setIsModulated(playing && isAssigned("fenv.decay"));
    filterDepth .setIsModulated(playing && isAssigned("fenv.depth"));
    ampAtk      .setIsModulated(playing && isAssigned("amp.attack"));
    ampDec      .setIsModulated(playing && isAssigned("amp.decay"));
    ampSus      .setIsModulated(playing && isAssigned("amp.sustain"));
    ampRel      .setIsModulated(playing && isAssigned("amp.release"));
    // #223 new destinations
    pitchDepth  .setIsModulated(playing && isAssigned("pitch.envDepth"));
    ampLevel    .setIsModulated(playing && isAssigned("amp.level"));
    ampAccent   .setIsModulated(playing && isAssigned("accentDb"));
    // Insert knobs map differently per algorithm; tag both possible meanings.
    insertDrive  .setIsModulated(playing && (isAssigned("insert.drive") || isAssigned("insert.bits")));
    insertOutput .setIsModulated(playing && (isAssigned("insert.output") || isAssigned("insert.rate")));
    insertDither .setIsModulated(playing && isAssigned("insert.dither"));
    insertTone   .setIsModulated(playing && isAssigned("insert.lpf"));
    auto arc = [&](bool assigned, int idx) -> float {
        return (assigned && playing) ? sn(idx) : kNaN;
    };

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
    insertDrive  .setModulatedNorm(playing ? (isAssigned("insert.drive") ? sn(P::kSnapInsDrive)
                                          : isAssigned("insert.bits")  ? sn(P::kSnapInsBits) : kNaN)
                                         : kNaN);
    insertOutput .setModulatedNorm(arc(isAssigned("insert.output"),    P::kSnapInsOutput));
    insertDither .setModulatedNorm(arc(isAssigned("insert.dither"),    P::kSnapInsDither));
    insertTone   .setModulatedNorm(arc(isAssigned("insert.lpf"),       P::kSnapInsLpf));
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
    // #287: pitch envelope times now in seconds directly.
    pitchAtk    .setValue(p.pitchEnvAtk,         juce::dontSendNotification);
    pitchDec    .setValue(p.pitchEnvDec,         juce::dontSendNotification);
    pitchSus    .setValue(p.pitchEnvSus * 100.0, juce::dontSendNotification);
    pitchRel    .setValue(p.pitchEnvRel,         juce::dontSendNotification);
    pitchDepth  .setValue(p.pitchEnvDepth,       juce::dontSendNotification);

    filterType.setSelectedId(p.filterType + 1, false);
    filterCutoff.setValue(p.filterCutoff,           juce::dontSendNotification);
    filterRes   .setValue(p.filterRes * 100.0,      juce::dontSendNotification);
    // #286: filter envelope times now in seconds directly.
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
    // #217: amp slider value IS seconds now (no conversion); Sustain stays 0..100 %.
    ampAtk  .setValue(p.ampEnvAtk,                                  juce::dontSendNotification);
    ampDec  .setValue(p.ampEnvDec,                                  juce::dontSendNotification);
    ampSus  .setValue(p.ampEnvSus  * 100.0,                         juce::dontSendNotification);
    ampRel  .setValue(p.ampRelToEnd ? 10.0 : p.ampEnvRel,            juce::dontSendNotification);

    insertAlgo.setSelectedId(p.insertAlgo + 1, false);
    configureInsertAlgorithm(p.insertAlgo);  // sets all insert knob ranges/labels/values/callbacks
}

void VoiceSection::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& p = proc.getRhythm(rhythmIndex).voiceParams;
    constexpr auto dn = juce::dontSendNotification;

    // ── Pitch
    if      (suffix == "pitchOct")   pitchOctave.setValue(p.pitchOctave,    dn);
    else if (suffix == "pitchSemi")  pitchSemi  .setValue(p.pitchSemitones, dn);
    else if (suffix == "pitchFine")  pitchFine  .setValue(p.pitchFine,      dn);
    else if (suffix == "pEnvAtk")    pitchAtk   .setValue(p.pitchEnvAtk,    dn);
    else if (suffix == "pEnvDec")    pitchDec   .setValue(p.pitchEnvDec,    dn);
    else if (suffix == "pEnvSus")    pitchSus   .setValue(p.pitchEnvSus * 100.0, dn);
    else if (suffix == "pEnvRel")    pitchRel   .setValue(p.pitchEnvRel,    dn);
    else if (suffix == "pEnvDep")    pitchDepth .setValue(p.pitchEnvDepth,  dn);
    // ── Filter
    else if (suffix == "fltType")    filterType.setSelectedId(p.filterType + 1, false);
    else if (suffix == "fltCut")     filterCutoff.setValue(p.filterCutoff,        dn);
    else if (suffix == "fltRes")     filterRes   .setValue(p.filterRes * 100.0,   dn);
    else if (suffix == "fEnvAtk")    filterAtk   .setValue(p.filterEnvAtk,        dn);
    else if (suffix == "fEnvDec")    filterDec   .setValue(p.filterEnvDec,        dn);
    else if (suffix == "fEnvSus")    filterSus   .setValue(p.filterEnvSus * 100.0, dn);
    else if (suffix == "fEnvRel")    filterRel   .setValue(p.filterEnvRel,        dn);
    else if (suffix == "fEnvDep")    filterDepth .setValue(p.filterEnvDepth,      dn);
    // ── Amp
    else if (suffix == "ampLvl")     ampLevel .setValue(p.ampLevel,                              dn);
    else if (suffix == "accentDb")   ampAccent.setValue(p.accentDb,                              dn);
    else if (suffix == "aEnvAtk")    ampAtk   .setValue(p.ampEnvAtk,                             dn);
    else if (suffix == "aEnvDec")    ampDec   .setValue(p.ampEnvDec,                             dn);
    else if (suffix == "aEnvSus")    ampSus   .setValue(p.ampEnvSus * 100.0,                     dn);
    else if (suffix == "aEnvRel")    ampRel   .setValue(p.ampRelToEnd ? 10.0 : p.ampEnvRel,      dn);
    // ── Sends (ch{ri}_ prefix)
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
    // ── Insert: algorithm change rebuilds the whole insert column. Per-knob updates
    // (drive/output/dither/tone/bits/rate/insertEqMid) re-run configureInsertAlgorithm
    // which restores all four knob values from the snapshot for the current algorithm —
    // cheaper than a full loadFromRhythm and correct because the layout depends on insertAlgo.
    else if (suffix == "drvChar"
          || suffix == "drvDrv" || suffix == "drvOut" || suffix == "drvDit" || suffix == "drvTon"
          || suffix == "drvBits" || suffix == "drvRate" || suffix == "eqMidGain")
    {
        if (suffix == "drvChar") insertAlgo.setSelectedId(p.insertAlgo + 1, false);
        configureInsertAlgorithm(p.insertAlgo);
    }
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
    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
        k->onValueChanged = nullptr;
    insertOutput.setGRSource(nullptr);  // #246: cleared here; comp/limiter cases re-set below

    // #423-followups: reset any grey-out applied by a previous Vocoder noise-carrier
    // configuration so the next algorithm doesn't inherit the disabled state.
    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
    {
        k->setEnabled(true);
        k->setAlpha(1.0f);
    }

    const VoiceParams* p = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms())
                           ? &proc.getRhythm(rhythmIndex).voiceParams : nullptr;

    switch (charId)
    {
        case 0:  // ── None — hide all insert knobs ──────────────────────────
            insertDrive .setVisible(false);
            insertOutput.setVisible(false);
            insertDither.setVisible(false);
            insertTone  .setVisible(false);
            break;

        case 1: case 2: case 3:  // ── Soft Clip / Hard Clip / Fold ─────────
            insertDrive.setLabel("Drive");
            insertDrive.setRange(0.0, 100.0, 0.1);
            // #245: display 0..100 as 0..40 dB input gain (`preGain = pow(10, drvDrv/100 * 2)`).
            // APVTS storage stays 0..100 — no preset-compat break, formatter only.
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String(v * 0.4, 1) + " dB";
            };
            insertDrive.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return juce::jlimit(0.0, 100.0, s.retainCharacters("-0123456789.").getDoubleValue() * 2.5);
            };
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output");
            insertOutput.setRange(-24.0, 0.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = nullptr;
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setVisible(false);

            insertTone.setLabel("LPF");
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);   // #289
            insertTone.getSlider().textFromValueFunction = fmtHz;
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(p->insertTone, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        case 4:  // ── Bitcrusher ───────────────────────────────────────────
            insertDrive.setLabel("Bits");
            insertDrive.setRange(1.0, 16.0, 1.0);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)v) + " bits";
            };
            insertDrive.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return juce::jlimit(1.0, 16.0, s.retainCharacters("0123456789").getDoubleValue());
            };
            if (p) insertDrive.setValue(p->insertBits, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Rate");
            insertOutput.setRange(100.0, 48000.0, 1.0);
            insertOutput.getSlider().setSkewFactorFromMidPoint(2190.0); // log feel: geometric mean of range
            insertOutput.getSlider().textFromValueFunction = fmtHz;
            insertOutput.getSlider().valueFromTextFunction = parseHz;
            if (p) insertOutput.setValue(p->insertRate, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Dither");
            insertDither.setRange(0.0, 100.0, 0.1);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            insertDither.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                return s.retainCharacters("0123456789.").getDoubleValue();
            };
            if (p) insertDither.setValue(p->insertDither, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel("LPF");
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);   // #289
            insertTone.getSlider().textFromValueFunction = fmtHz;
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(p->insertTone, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvBits", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvRate", (float)v); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit",  (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon",  (float)v); };
            break;

        case 5:  // ── Clipper — threshold + output, post-LPF ──────────────────
            insertDrive.setLabel("Threshold");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                // Display as "ceiling %" — at 100 the clipper is open, at 0 fully clamped.
                return juce::String((int)std::round(v)) + "%";
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output");
            insertOutput.setRange(-24.0, 0.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = nullptr;
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setVisible(false);

            insertTone.setLabel("LPF");
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);   // #289
            insertTone.getSlider().textFromValueFunction = fmtHz;
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(p->insertTone, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        case 6:  // ── 3-Band EQ: Low shelf / Mid peak / High shelf ──────────
        {
            auto dbFmt = [](double v) -> juce::String {
                return (v >= 0.0 ? "+" : "") + juce::String(v, 1) + " dB";
            };
            auto dbParse = [](const juce::String& s) -> double {
                return s.retainCharacters("0123456789.-+").getDoubleValue();
            };

            insertDrive.setLabel("Low");
            insertDrive.setRange(-18.0, 18.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = dbFmt;
            insertDrive.getSlider().valueFromTextFunction = dbParse;
            // insertDrive stores low gain as 0..100 (50 = 0 dB): load back
            if (p) insertDrive.setValue(p->insertDrive / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("High");
            insertOutput.setRange(-18.0, 18.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = dbFmt;
            insertOutput.getSlider().valueFromTextFunction = dbParse;
            // high gain stored in insertDither (0..100): load back
            if (p) insertOutput.setValue(p->insertDither / 100.0 * 36.0 - 18.0, juce::dontSendNotification);
            else   insertOutput.setValue(0.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Mid");
            insertDither.setRange(-18.0, 18.0, 0.1);
            insertDither.getSlider().textFromValueFunction = dbFmt;
            insertDither.getSlider().valueFromTextFunction = dbParse;
            if (p) insertDither.setValue(p->insertEqMid, juce::dontSendNotification);
            else   insertDither.setValue(0.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel("Mid Hz");
            insertTone.getSlider().setSkewFactor(1.0);  // reset any log skew from Compressor mode
            insertTone.setRange(200.0, 8000.0, 1.0);
            insertTone.getSlider().textFromValueFunction = fmtHz;
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(200.0, 8000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(1000.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            // Low gain -18..18 → stored as 0..100 in drvDrv
            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)((v + 18.0) / 36.0 * 100.0)); };
            // High gain -18..18 → stored as 0..100 in drvDit (reused slot)
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvDit", (float)((v + 18.0) / 36.0 * 100.0)); };
            // Mid gain → direct insertEqMid param
            insertDither.onValueChanged = [this](double v) { apvtsSet("eqMidGain", (float)v); };
            // Mid frequency → drvTon
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;
        }

        case 7: case 8:  // ── Compressor / Limiter ──────────────────────────
        {
            insertDrive.setLabel(charId == 8 ? "Ceiling" : "Threshold");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return "-" + juce::String((int)std::round(v * 0.4)) + " dB";
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            else   insertDrive.setValue(50.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output");
            insertOutput.setRange(-24.0, 24.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = nullptr;
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            else   insertOutput.setValue(0.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Attack");
            insertDither.setRange(0.0, 100.0, 0.1);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v * 2.0)) + " ms";
            };
            insertDither.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDither.setValue(p->insertDither, juce::dontSendNotification);
            else   insertDither.setValue(5.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel("Release");
            insertTone.setRange(20.0, 2000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(200.0);
            insertTone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return v < 1000.0 ? juce::String((int)v) + " ms"
                                  : juce::String(v / 1000.0, 2) + " s";
            };
            insertTone.getSlider().valueFromTextFunction = [](const juce::String& s) -> double {
                auto t = s.trim().toLowerCase();
                if (t.endsWith("ms")) return t.dropLastCharacters(2).trim().getDoubleValue();
                if (t.endsWith("s"))  return t.dropLastCharacters(1).trim().getDoubleValue() * 1000.0;
                return t.getDoubleValue();
            };
            if (p) insertTone.setValue(juce::jlimit(20.0, 2000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(100.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };

            // #246: GR meter on the Output knob
            {
                auto* ve = (rhythmIndex >= 0 && rhythmIndex < proc.getNumRhythms()
                            && proc.voiceEngines[rhythmIndex])
                           ? proc.voiceEngines[rhythmIndex].get() : nullptr;
                insertOutput.setGRSource(ve ? &ve->insertProc.grReduction : nullptr);
            }
            break;
        }

        case 9:  // ── Ring Modulator — Mix + Freq ───────────────────────────
            insertDrive.setLabel("Mix");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int)std::round(v)) + "%";
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            else   insertDrive.setValue(50.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setVisible(false);
            insertDither.setVisible(false);

            insertTone.setLabel("Freq");
            insertTone.getSlider().setSkewFactorFromMidPoint(223.6);
            insertTone.setRange(10.0, 5000.0, 1.0);
            insertTone.getSlider().textFromValueFunction = fmtHz;
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(10.0, 5000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(440.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive.onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertTone .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        case 10:  // ── Tape Saturation — Drive / Output / Tone ─────────────
            insertDrive.setLabel("Drive");
            insertDrive.setRange(0.0, 100.0, 0.1);
            insertDrive.getSlider().textFromValueFunction = nullptr;
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(p->insertDrive, juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Output");
            insertOutput.setRange(-24.0, 0.0, 0.1);
            insertOutput.getSlider().textFromValueFunction = nullptr;
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            if (p) insertOutput.setValue(p->insertOutput, juce::dontSendNotification);
            else   insertOutput.setValue(0.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setVisible(false);

            insertTone.setLabel("Tone");
            insertTone.getSlider().setSkewFactor(1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(2000.0);
            insertTone.setRange(200.0, 20000.0, 1.0);
            insertTone.getSlider().textFromValueFunction = fmtHz;
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(200.0, 20000.0, (double)p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(10000.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv", (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut", (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon", (float)v); };
            break;

        case 11:  // #422 ── Karplus-Strong — Note / Octave / Feedback / LPF ─
        {
            static const char* const kNoteNames[7] = { "C", "D", "E", "F", "G", "A", "B" };
            auto noteFmt = [](double v) -> juce::String {
                const int i = juce::jlimit(0, 6, (int) std::round(v));
                return kNoteNames[i];
            };

            insertDrive.setLabel("Note");
            insertDrive.setRange(0.0, 6.0, 1.0);
            insertDrive.getSlider().setSkewFactor(1.0);
            insertDrive.getSlider().textFromValueFunction = noteFmt;
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(juce::jlimit(0.0, 6.0, (double) p->insertDrive), juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            insertOutput.setLabel("Octave");
            // #429: range extended to 0..3 (was 1..3). Octave 0 = SPN C1 = 32.7 Hz.
            insertOutput.setRange(0.0, 3.0, 1.0);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int) std::round(v));
            };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            // Octave is stored in insertBits (not drvOut) — see KarplusStrongInsert.
            if (p) insertOutput.setValue(juce::jlimit(0.0, 3.0, (double) p->insertBits), juce::dontSendNotification);
            else   insertOutput.setValue(1.0, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Feedback");
            insertDither.setRange(0.0, 100.0, 0.1);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int) std::round(v)) + "%";
            };
            insertDither.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDither.setValue(p->insertDither, juce::dontSendNotification);
            else   insertDither.setValue(70.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            insertTone.setLabel("LPF");
            insertTone.setRange(20.0, 20000.0, 1.0);
            insertTone.getSlider().setSkewFactorFromMidPoint(640.0);   // log feel
            insertTone.getSlider().textFromValueFunction = fmtHz;
            insertTone.getSlider().valueFromTextFunction = parseHz;
            if (p) insertTone.setValue(juce::jlimit(20.0, 20000.0, (double) p->insertTone), juce::dontSendNotification);
            else   insertTone.setValue(20000.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            insertDrive .onValueChanged = [this](double v) { apvtsSet("drvDrv",  (float)v); };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvBits", (float)v); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit",  (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvTon",  (float)v); };
            break;
        }

        case 12:  // #423 ── Vocoder — Wave / Note / Octave / Unison ────────
        {
            static const char* const kWaveNames[4] = { "Saw", "Square", "White", "Pink" };
            static const char* const kNoteNames[7] = { "C", "D", "E", "F", "G", "A", "B" };
            static const int kUnisonCounts[7] = { 1, 3, 5, 7, 9, 11, 13 };

            insertDrive.setLabel("Wave");
            insertDrive.setRange(0.0, 3.0, 1.0);
            insertDrive.getSlider().setSkewFactor(1.0);
            insertDrive.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kWaveNames[juce::jlimit(0, 3, (int) std::round(v))];
            };
            insertDrive.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDrive.setValue(juce::jlimit(0.0, 3.0, (double) p->insertDrive), juce::dontSendNotification);
            else   insertDrive.setValue(0.0, juce::dontSendNotification);
            insertDrive.setVisible(true);

            // #428: Unison moved from insertTone (20..20k range — would clamp
            // small ints to 20 and produce a stuck 13-voice default) to
            // insertOutput (-24..0 range). UI knob value is 0..6 (unison
            // index); we encode to drvOut via `v * 4 - 24` on write and
            // decode in VocoderInsert via `(drvOut + 24) / 4`.
            insertOutput.setLabel("Unison");
            insertOutput.setRange(0.0, 6.0, 1.0);
            insertOutput.getSlider().setSkewFactor(1.0);
            insertOutput.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String(kUnisonCounts[juce::jlimit(0, 6, (int) std::round(v))]);
            };
            insertOutput.getSlider().valueFromTextFunction = nullptr;
            // Decode stored drvOut (-24..0) back to UI knob value (0..6).
            const double unisonKnob = p ? juce::jlimit(0.0, 6.0,
                                            ((double) p->insertOutput + 24.0) * 0.25)
                                        : 1.0;
            insertOutput.setValue(unisonKnob, juce::dontSendNotification);
            insertOutput.setVisible(true);

            insertDither.setLabel("Octave");
            insertDither.setRange(1.0, 5.0, 1.0);
            insertDither.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return juce::String((int) std::round(v));
            };
            insertDither.getSlider().valueFromTextFunction = nullptr;
            if (p) insertDither.setValue(juce::jlimit(1.0, 5.0, (double) p->insertDither), juce::dontSendNotification);
            else   insertDither.setValue(3.0, juce::dontSendNotification);
            insertDither.setVisible(true);

            // #428: re-use the insertTone knob slot for Note (the Karplus
            // pattern would have used insertBits, but for Vocoder we already
            // need insertBits for the +1 note offset). Show note letter via
            // textFromValueFunction.
            insertTone.setLabel("Note");
            insertTone.getSlider().setSkewFactor(1.0);
            insertTone.setRange(1.0, 7.0, 1.0);
            insertTone.getSlider().textFromValueFunction = [](double v) -> juce::String {
                return kNoteNames[juce::jlimit(0, 6, (int) std::round(v) - 1)];
            };
            insertTone.getSlider().valueFromTextFunction = nullptr;
            if (p) insertTone.setValue(juce::jlimit(1.0, 7.0, (double) p->insertBits), juce::dontSendNotification);
            else   insertTone.setValue(4.0, juce::dontSendNotification);
            insertTone.setVisible(true);

            // #423-followups: grey out Note / Octave / Unison when the carrier is
            // White (2) or Pink (3) noise — the algorithm ignores those controls
            // (no fundamental to detune), so leaving them active was a usability
            // trap. Helper closure updates the enabled state from the current Wave
            // value; called once now from the initial value and re-called from the
            // Wave knob's onValueChanged below.
            auto syncVocoderGreyOut = [this]
            {
                const int wave = juce::jlimit(0, 3, (int) std::round(insertDrive.getValue()));
                const bool pitched = (wave < 2);
                insertDither.setEnabled(pitched);
                insertOutput.setEnabled(pitched);
                insertTone  .setEnabled(pitched);
                insertDither.setAlpha(pitched ? 1.0f : 0.45f);
                insertOutput.setAlpha(pitched ? 1.0f : 0.45f);
                insertTone  .setAlpha(pitched ? 1.0f : 0.45f);
            };
            syncVocoderGreyOut();

            insertDrive .onValueChanged = [this, syncVocoderGreyOut](double v)
            {
                apvtsSet("drvDrv", (float)v);
                syncVocoderGreyOut();
            };
            insertOutput.onValueChanged = [this](double v) { apvtsSet("drvOut",  (float)(v * 4.0 - 24.0)); };
            insertDither.onValueChanged = [this](double v) { apvtsSet("drvDit",  (float)v); };
            insertTone  .onValueChanged = [this](double v) { apvtsSet("drvBits", (float)v); };
            break;
        }

        default: break;
    }

    // Refresh display text and reposition knobs
    for (auto* k : { &insertDrive, &insertOutput, &insertDither, &insertTone })
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
    // Dropdown spans 2 cols so labels like "Notch 24" fit; knobs shift right by 1.
    filterType  .setBounds(filterX + 0 * kW, row1Y + rowH / 4, 2 * kW, rowH / 2);
    filterCutoff.setBounds(filterX + 2 * kW, row1Y, kW, rowH);
    filterRes   .setBounds(filterX + 3 * kW, row1Y, kW, rowH);
    filterDepth .setBounds(filterX + 4 * kW, row1Y, kW, rowH);

    filterAtk.setBounds(filterX + 1 * kW, row2Y, kW, rowH);
    filterDec.setBounds(filterX + 2 * kW, row2Y, kW, rowH);
    filterSus.setBounds(filterX + 3 * kW, row2Y, kW, rowH);
    filterRel.setBounds(filterX + 4 * kW, row2Y, kW, rowH);

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
    insertAlgo.setBounds(driveX, row1Y + rowH / 4, 4 * kW, rowH / 2);
    // Bitcrusher (id=5), EQ (id=7), Compressor (id=8), Limiter (id=9) use 4 knob slots.
    // Ring Mod (id=10) and Tape Sat (id=11) use 2 and 3 slots respectively.
    const int selId = insertAlgo.getSelectedId();
    const bool showFour = (selId == 5 || (selId >= 7 && selId <= 9));
    insertDrive .setBounds(driveX + 0 * kW, row2Y, kW, rowH);
    insertOutput.setBounds(driveX + 1 * kW, row2Y, kW, rowH);
    if (showFour)
    {
        insertDither.setBounds(driveX + 2 * kW, row2Y, kW, rowH);
        insertTone  .setBounds(driveX + 3 * kW, row2Y, kW, rowH);
    }
    else
    {
        insertTone  .setBounds(driveX + 2 * kW, row2Y, kW, rowH);
        insertDither.setBounds(driveX + 3 * kW, row2Y, kW, rowH);  // hidden, off-screen position
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
