#include "EuclideanPanel.h"
#include "Plugin/PluginProcessor.h"
#include "Modulation/ModulationSnapshot.h"
#include <limits>

EuclideanPanel::EuclideanPanel(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &stepsA, &hitsA, &rotA, &prePadA, &postPadA, &insertStA, &insertLenA,
                     &stepsB, &hitsB, &rotB, &prePadB, &postPadB, &insertStB, &insertLenB,
                     &stepsC, &hitsC, &rotC, &prePadC, &postPadC, &insertStC, &insertLenC })
        addAndMakeVisible(k);

    for (auto* s : { &prePadModeA, &postPadModeA, &insertModeA,
                     &prePadModeB, &postPadModeB, &insertModeB,
                     &prePadModeC, &postPadModeC, &insertModeC })
        addAndMakeVisible(s);

    addAndMakeVisible(legatoCtrl);
    addAndMakeVisible(monoCtrl);

    // Logic dropdown (replaced the 5-pill SegmentControl) — populated with
    // 1-based IDs that map to APVTS "logic" param via id - 1.
    addAndMakeVisible(logicCtrl);
    logicCtrl.addItem("OR",      1);
    logicCtrl.addItem("AND",     2);
    logicCtrl.addItem("XOR",     3);
    logicCtrl.addItem("A not B", 4);
    logicCtrl.addItem("B not A", 5);

    stepsA.setRange(1, 64, 1);      hitsA.setRange(0, 64, 1);   rotA.setRange(0, 63, 1);
    prePadA.setRange(0, 12, 1);     postPadA.setRange(0, 12, 1);
    insertStA.setRange(0, 63, 1);   insertLenA.setRange(0, 8, 1);

    stepsB.setRange(1, 64, 1);      hitsB.setRange(0, 64, 1);   rotB.setRange(0, 63, 1);
    prePadB.setRange(0, 12, 1);     postPadB.setRange(0, 12, 1);
    insertStB.setRange(0, 63, 1);   insertLenB.setRange(0, 8, 1);

    stepsC.setRange(1, 64, 1);      hitsC.setRange(0, 64, 1);   rotC.setRange(0, 63, 1);
    prePadC.setRange(0, 12, 1);     postPadC.setRange(0, 12, 1);
    insertStC.setRange(0, 63, 1);   insertLenC.setRange(0, 8, 1);

    stepsA.setValue(8); stepsB.setValue(8); stepsC.setValue(8);

    wireCallbacks();
}

void EuclideanPanel::apvtsSet(const char* suffix, float v)
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

// Pad keeps every hit: the zone becomes rests and the hits are redistributed over the
// steps left. Mute keeps the pattern where it is and silences whatever lands in the zone.
juce::String EuclideanPanel::padModeExplanation(PadZone zone, int modeIndex)
{
    const char* where = zone == PadZone::Start ? "at the start"
                      : zone == PadZone::End   ? "at the end"
                                               : "at Insert Start";
    if (modeIndex == 1)
        return juce::String("Mute - pattern spans every step; hits ") + where + " are silenced";
    return juce::String("Pad - silent steps ") + where + "; all hits fit into the rest";
}

void EuclideanPanel::wireCallbacks()
{
    auto notify = [this] { if (onPatternChanged) onPatternChanged(); };

    // ── Euclid A ─────────────────────────────────────────────────────────────
    stepsA.onValueChanged = [this, notify](double v) {
        apvtsSet("stepsA", (float)v);
        updateRangesA((int)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Steps", juce::String((int)v));
    };
    hitsA.onValueChanged = [this, notify](double v) {
        apvtsSet("hitsA", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Hits", juce::String((int)v));
    };
    rotA.onValueChanged = [this, notify](double v) {
        apvtsSet("rotA", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Rotate", juce::String((int)v));
    };
    prePadA.onValueChanged = [this, notify](double v) {
        apvtsSet("prePadA", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Pre Pad", juce::String((int)v));
    };
    postPadA.onValueChanged = [this, notify](double v) {
        apvtsSet("postPadA", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Post Pad", juce::String((int)v));
    };
    insertStA.onValueChanged = [this, notify](double v) {
        apvtsSet("insStA", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Start", juce::String((int)v));
    };
    insertLenA.onValueChanged = [this, notify](double v) {
        apvtsSet("insLenA", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Length", juce::String((int)v));
    };
    prePadModeA.onChange = [this, notify](int idx) {
        apvtsSet("prePadModeA", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Pre Pad Mode", padModeExplanation(PadZone::Start, idx));
    };
    postPadModeA.onChange = [this, notify](int idx) {
        apvtsSet("postPadModeA", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Post Pad Mode", padModeExplanation(PadZone::End, idx));
    };
    insertModeA.onChange = [this, notify](int idx) {
        apvtsSet("insModeA", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Mode", padModeExplanation(PadZone::Insert, idx));
    };

    // ── Legato ─────────────────────────────────────────────────────────
    // Trig (default) = every step retriggers the envelope.
    // Leg              = contiguous hits skip the envelope retrigger; the
    //                    envelope state continues across the run, and the
    //                    sample voice gets a short fade-in to mask the
    //                    waveform discontinuity at sample[0].
    legatoCtrl.onChange = [this, notify](int idx) {
        apvtsSet("patLeg", idx > 0 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Pattern Legato", idx > 0 ? "On" : "Off");
    };

    // Mono = polyphony cap. VoiceEngine::trigger forces voices[0] when active.
    // Independent of legato — Legato controls retrigger behaviour on the
    // single voice that exists in mono mode.
    monoCtrl.onChange = [this, notify](int idx) {
        apvtsSet("vMono", idx > 0 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Voice Mode", idx > 0 ? "Mono" : "Poly");
    };

    // ── Logic ─────────────────────────────────────────────────────────────────
    // DropdownSelect fires onChange with the 1-based ComboBox ID — convert to
    // the 0-based index used by the APVTS "logic" param.
    static const char* const logicNames[] = { "OR", "AND", "XOR", "A not B", "B not A" };
    logicCtrl.onChange = [this, notify](int id) {
        const int idx = id - 1;
        apvtsSet("logic", (float)idx);  notify();
        if (onStatusUpdate && idx >= 0 && idx < 5)
            onStatusUpdate("Logic", juce::String(logicNames[idx]));
    };

    // ── Euclid B ─────────────────────────────────────────────────────────────
    stepsB.onValueChanged = [this, notify](double v) {
        apvtsSet("stepsB", (float)v);
        updateRangesB((int)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Steps", juce::String((int)v));
    };
    hitsB.onValueChanged = [this, notify](double v) {
        apvtsSet("hitsB", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Hits", juce::String((int)v));
    };
    rotB.onValueChanged = [this, notify](double v) {
        apvtsSet("rotB", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Rotate", juce::String((int)v));
    };
    prePadB.onValueChanged = [this, notify](double v) {
        apvtsSet("prePadB", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Pre Pad", juce::String((int)v));
    };
    postPadB.onValueChanged = [this, notify](double v) {
        apvtsSet("postPadB", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Post Pad", juce::String((int)v));
    };
    insertStB.onValueChanged = [this, notify](double v) {
        apvtsSet("insStB", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Start", juce::String((int)v));
    };
    insertLenB.onValueChanged = [this, notify](double v) {
        apvtsSet("insLenB", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Length", juce::String((int)v));
    };
    prePadModeB.onChange = [this, notify](int idx) {
        apvtsSet("prePadModeB", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Pre Pad Mode", padModeExplanation(PadZone::Start, idx));
    };
    postPadModeB.onChange = [this, notify](int idx) {
        apvtsSet("postPadModeB", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Post Pad Mode", padModeExplanation(PadZone::End, idx));
    };
    insertModeB.onChange = [this, notify](int idx) {
        apvtsSet("insModeB", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Mode", padModeExplanation(PadZone::Insert, idx));
    };

    // ── Euclid C (Accent) ─────────────────────────────────────────────────────
    stepsC.onValueChanged = [this, notify](double v) {
        apvtsSet("stepsC", (float)v);
        updateRangesC((int)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Steps", juce::String((int)v));
    };
    hitsC.onValueChanged = [this, notify](double v) {
        apvtsSet("hitsC", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Hits", juce::String((int)v));
    };
    rotC.onValueChanged = [this, notify](double v) {
        apvtsSet("rotC", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Rotate", juce::String((int)v));
    };
    prePadC.onValueChanged = [this, notify](double v) {
        apvtsSet("prePadC", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Pre Pad", juce::String((int)v));
    };
    postPadC.onValueChanged = [this, notify](double v) {
        apvtsSet("postPadC", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Post Pad", juce::String((int)v));
    };
    insertStC.onValueChanged = [this, notify](double v) {
        apvtsSet("insStC", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Start", juce::String((int)v));
    };
    insertLenC.onValueChanged = [this, notify](double v) {
        apvtsSet("insLenC", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Length", juce::String((int)v));
    };
    prePadModeC.onChange = [this, notify](int idx) {
        apvtsSet("prePadModeC", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Pre Pad Mode", padModeExplanation(PadZone::Start, idx));
    };
    postPadModeC.onChange = [this, notify](int idx) {
        apvtsSet("postPadModeC", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Post Pad Mode", padModeExplanation(PadZone::End, idx));
    };
    insertModeC.onChange = [this, notify](int idx) {
        apvtsSet("insModeC", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Mode", padModeExplanation(PadZone::Insert, idx));
    };
}

void EuclideanPanel::setRhythm(int ri)
{
    if (ri != rhythmIndex)
        paramPtrCache.clear();   // cached pointers were keyed to the previous rhythmIndex's IDs
    rhythmIndex = ri;
    loadFromRhythm();
    bindModulationIndicators();
}

void EuclideanPanel::loadFromRhythm()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const Rhythm& r = proc.getRhythm(rhythmIndex);

    stepsA.setValue(r.genA.steps);   hitsA.setValue(r.genA.hits);
    rotA.setValue(r.genA.rotate);    prePadA.setValue(r.genA.prePad);
    postPadA.setValue(r.genA.postPad);
    insertStA.setValue(r.genA.insertStart);
    insertLenA.setValue(r.genA.insertLength);
    prePadModeA.setSelectedIndex(r.genA.prePadMode   == InsertMode::Mute ? 1 : 0);
    postPadModeA.setSelectedIndex(r.genA.postPadMode == InsertMode::Mute ? 1 : 0);
    insertModeA.setSelectedIndex(r.genA.insertMode   == InsertMode::Mute ? 1 : 0);
    updateRangesA(r.genA.steps);

    stepsB.setValue(r.genB.steps);   hitsB.setValue(r.genB.hits);
    rotB.setValue(r.genB.rotate);    prePadB.setValue(r.genB.prePad);
    postPadB.setValue(r.genB.postPad);
    insertStB.setValue(r.genB.insertStart);
    insertLenB.setValue(r.genB.insertLength);
    prePadModeB.setSelectedIndex(r.genB.prePadMode   == InsertMode::Mute ? 1 : 0);
    postPadModeB.setSelectedIndex(r.genB.postPadMode == InsertMode::Mute ? 1 : 0);
    insertModeB.setSelectedIndex(r.genB.insertMode   == InsertMode::Mute ? 1 : 0);
    updateRangesB(r.genB.steps);

    stepsC.setValue(r.genC.steps);   hitsC.setValue(r.genC.hits);
    rotC.setValue(r.genC.rotate);    prePadC.setValue(r.genC.prePad);
    postPadC.setValue(r.genC.postPad);
    insertStC.setValue(r.genC.insertStart);
    insertLenC.setValue(r.genC.insertLength);
    prePadModeC.setSelectedIndex(r.genC.prePadMode   == InsertMode::Mute ? 1 : 0);
    postPadModeC.setSelectedIndex(r.genC.postPadMode == InsertMode::Mute ? 1 : 0);
    insertModeC.setSelectedIndex(r.genC.insertMode   == InsertMode::Mute ? 1 : 0);
    updateRangesC(r.genC.steps);

    static const Logic logics[] = { Logic::OR, Logic::AND, Logic::XOR,
                                    Logic::AOnly, Logic::BOnly };
    for (int i = 0; i < 5; i++)
        if (r.logic == logics[i]) { logicCtrl.setSelectedId(i + 1); break; }

    legatoCtrl.setSelectedIndex(r.patternLegato ? 1 : 0);
    monoCtrl  .setSelectedIndex(r.voiceParams.voiceMono ? 1 : 0);
}

void EuclideanPanel::refreshSuffix(const juce::String& suffix)
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const Rhythm& r = proc.getRhythm(rhythmIndex);

    auto setMode = [](SlideSwitch& sc, InsertMode m) {
        sc.setSelectedIndex(m == InsertMode::Mute ? 1 : 0);
    };

    // ── Ring A
    if      (suffix == "stepsA")        { stepsA.setValue(r.genA.steps); updateRangesA(r.genA.steps); }
    else if (suffix == "hitsA")           hitsA.setValue(r.genA.hits);
    else if (suffix == "rotA")            rotA.setValue(r.genA.rotate);
    else if (suffix == "prePadA")         prePadA.setValue(r.genA.prePad);
    else if (suffix == "postPadA")        postPadA.setValue(r.genA.postPad);
    else if (suffix == "insStA")          insertStA.setValue(r.genA.insertStart);
    else if (suffix == "insLenA")         insertLenA.setValue(r.genA.insertLength);
    else if (suffix == "prePadModeA")     setMode(prePadModeA, r.genA.prePadMode);
    else if (suffix == "postPadModeA")    setMode(postPadModeA, r.genA.postPadMode);
    else if (suffix == "insModeA")        setMode(insertModeA, r.genA.insertMode);
    // ── Ring B
    else if (suffix == "stepsB")        { stepsB.setValue(r.genB.steps); updateRangesB(r.genB.steps); }
    else if (suffix == "hitsB")           hitsB.setValue(r.genB.hits);
    else if (suffix == "rotB")            rotB.setValue(r.genB.rotate);
    else if (suffix == "prePadB")         prePadB.setValue(r.genB.prePad);
    else if (suffix == "postPadB")        postPadB.setValue(r.genB.postPad);
    else if (suffix == "insStB")          insertStB.setValue(r.genB.insertStart);
    else if (suffix == "insLenB")         insertLenB.setValue(r.genB.insertLength);
    else if (suffix == "prePadModeB")     setMode(prePadModeB, r.genB.prePadMode);
    else if (suffix == "postPadModeB")    setMode(postPadModeB, r.genB.postPadMode);
    else if (suffix == "insModeB")        setMode(insertModeB, r.genB.insertMode);
    // ── Ring C (Accent)
    else if (suffix == "stepsC")        { stepsC.setValue(r.genC.steps); updateRangesC(r.genC.steps); }
    else if (suffix == "hitsC")           hitsC.setValue(r.genC.hits);
    else if (suffix == "rotC")            rotC.setValue(r.genC.rotate);
    else if (suffix == "prePadC")         prePadC.setValue(r.genC.prePad);
    else if (suffix == "postPadC")        postPadC.setValue(r.genC.postPad);
    else if (suffix == "insStC")          insertStC.setValue(r.genC.insertStart);
    else if (suffix == "insLenC")         insertLenC.setValue(r.genC.insertLength);
    else if (suffix == "prePadModeC")     setMode(prePadModeC, r.genC.prePadMode);
    else if (suffix == "postPadModeC")    setMode(postPadModeC, r.genC.postPadMode);
    else if (suffix == "insModeC")        setMode(insertModeC, r.genC.insertMode);
    // ── Logic
    else if (suffix == "logic")
    {
        static const Logic logics[] = { Logic::OR, Logic::AND, Logic::XOR,
                                        Logic::AOnly, Logic::BOnly };
        for (int i = 0; i < 5; i++)
            if (r.logic == logics[i]) { logicCtrl.setSelectedId(i + 1); break; }
    }
    // ── Legato
    else if (suffix == "patLeg")
        legatoCtrl.setSelectedIndex(r.patternLegato ? 1 : 0);
    // ── Voice Mono
    else if (suffix == "vMono")
        monoCtrl.setSelectedIndex(r.voiceParams.voiceMono ? 1 : 0);
}

void EuclideanPanel::updateRangesA(int steps)
{
    hitsA.setRange(0, steps, 1);
    rotA.setRange(0, juce::jmax(0, steps - 1), 1);   // full 0..steps-1 per design-sequencer.md
    insertStA.setRange(0, juce::jmax(0, steps - 1), 1);
}

void EuclideanPanel::updateRangesB(int steps)
{
    hitsB.setRange(0, steps, 1);
    rotB.setRange(0, juce::jmax(0, steps - 1), 1);
    insertStB.setRange(0, juce::jmax(0, steps - 1), 1);
}

void EuclideanPanel::updateRangesC(int steps)
{
    hitsC.setRange(0, steps, 1);
    rotC.setRange(0, juce::jmax(0, steps - 1), 1);
    insertStC.setRange(0, juce::jmax(0, steps - 1), 1);
}

void EuclideanPanel::setRhythmColour(juce::Colour c)
{
    rhythmColour = c;
    repaint();
}

void EuclideanPanel::bindModulationIndicators()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms())
    {
        for (auto* k : { &hitsA, &rotA, &prePadA, &postPadA, &insertStA, &insertLenA,
                         &hitsB, &rotB, &prePadB, &postPadB, &insertStB, &insertLenB,
                         &hitsC, &rotC, &prePadC, &postPadC, &insertStC, &insertLenC })
            k->clearModBinding();
        return;
    }
    const auto* mx = &proc.getRhythm(rhythmIndex).modulationMatrix;
    static const float kNaN = std::numeric_limits<float>::quiet_NaN();

    // Euclid destinations: modParamValues are 0..1 proportions → normMode=true for all.
    // Arc clears when sequencer stops.
    auto bind = [&](KnobWithLabel& k, const char* destId, int snapIndex)
    {
        k.bindModulation(destId, mx,
            [&proc = proc, ri = rhythmIndex, snapIndex]() -> float {
                return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, snapIndex) : kNaN; },
            true);
    };

    bind(hitsA,      "euclid.a.hits",    kSnapEucAHits);
    bind(rotA,       "euclid.a.rotate",  kSnapEucARotate);
    bind(prePadA,    "euclid.a.prePad",  kSnapEucAPrePad);
    bind(postPadA,   "euclid.a.postPad", kSnapEucAPostPad);
    bind(insertStA,  "euclid.a.insSt",   kSnapEucAInsSt);
    bind(insertLenA, "euclid.a.insLen",  kSnapEucAInsLen);

    bind(hitsB,      "euclid.b.hits",    kSnapEucBHits);
    bind(rotB,       "euclid.b.rotate",  kSnapEucBRotate);
    bind(prePadB,    "euclid.b.prePad",  kSnapEucBPrePad);
    bind(postPadB,   "euclid.b.postPad", kSnapEucBPostPad);
    bind(insertStB,  "euclid.b.insSt",   kSnapEucBInsSt);
    bind(insertLenB, "euclid.b.insLen",  kSnapEucBInsLen);

    bind(hitsC,      "euclid.c.hits",    kSnapEucCHits);
    bind(rotC,       "euclid.c.rotate",  kSnapEucCRotate);
    bind(prePadC,    "euclid.c.prePad",  kSnapEucCPrePad);
    bind(postPadC,   "euclid.c.postPad", kSnapEucCPostPad);
    bind(insertStC,  "euclid.c.insSt",   kSnapEucCInsSt);
    bind(insertLenC, "euclid.c.insLen",  kSnapEucCInsLen);
}

void EuclideanPanel::resized()
{
    // Fixed Medium-baseline layout — see MuLookAndFeel for the constants.
    constexpr int innerW = MuLookAndFeel::kEuclidInnerW - 2 * kOuter;   // panel width minus its 4 px border
    constexpr int innerH = MuLookAndFeel::kEuclidInnerH - 2 * kOuter;

    constexpr int rowH  = (innerH - kLogicH) / 3;
    constexpr int ctrlH = rowH - kLabelH;   // control zone within each row (below label)

    // Steps/Hits/Rotate render at Size 1 (canonical). kEucKnobGap
    // separates the three knobs visually; the whole block then defines where
    // the Pad sub-panel begins, so the row's right-hand columns shrink to
    // absorb the extra width.
    constexpr int eW    = MuLookAndFeel::kKnobSize1W;
    constexpr int eH    = MuLookAndFeel::kKnobSize1H;
    constexpr int eucBlockW = eW * 3 + kEucKnobGap * 2;
    constexpr int pW    = (innerW - eucBlockW) / 4;
    constexpr int padX  = kOuter + eucBlockW;
    // kPadInsertGap splits the Pad and Insert sub-panel borders so they
    // no longer share a pixel. Half the gap is taken from each side.
    constexpr int padPanelW = pW * 2 - kPadInsertGap / 2;
    constexpr int insX      = padX + pW * 2 + kPadInsertGap / 2;
    constexpr int insPanelW = MuLookAndFeel::kEuclidInnerW - kOuter - insX;

    // Pad and Insert sub-panels: Size 2 knobs, each Pad/Mute slide switch beside its
    // knob, all centred vertically in the sub-panel's box (ctrlH - 2 tall, as painted).
    constexpr int knobW  = MuLookAndFeel::kKnobSize2W;
    constexpr int knobH  = MuLookAndFeel::kKnobSize2H;
    constexpr int swW    = MuLookAndFeel::kSlideSwitchW;
    constexpr int swH    = MuLookAndFeel::kSlideSwitchH;
    constexpr int knobDY = (ctrlH - 2 - knobH) / 2;
    constexpr int swDY   = (ctrlH - 2 - swH) / 2;

    // Pad sub-panel: two [knob | switch] units side by side.
    constexpr int unitW    = knobW + kSwitchGap + swW;
    constexpr int padSpan  = unitW * 2 + kSwitchGap;
    constexpr int prePadX  = padX + (padPanelW - padSpan) / 2;
    constexpr int postPadX = prePadX + unitW + kSwitchGap;

    // Insert sub-panel: the two knobs share one switch, placed to their right.
    constexpr int insSpan = knobW * 2 + kInsKnobGap + kSwitchGap * 2 + swW;
    constexpr int insStX  = insX + (insPanelW - insSpan) / 2;
    constexpr int insLenX = insStX + knobW + kInsKnobGap;
    constexpr int insSwX  = insLenX + knobW + kSwitchGap * 2;

    // Every literal/constant in setBounds wrapped in mu_ui::s() so toggling
    // the UI scale propagates uniformly. Identity at scale = 1.0.
    using mu_ui::s;

    auto placeRow = [&](int y,
                        KnobWithLabel& steps, KnobWithLabel& hits, KnobWithLabel& rot,
                        KnobWithLabel& prePad, KnobWithLabel& postPad,
                        SlideSwitch& prePadMode, SlideSwitch& postPadMode,
                        KnobWithLabel& insSt, KnobWithLabel& insLen,
                        SlideSwitch& insMode)
    {
        const int cy = y + kLabelH;  // top of control zone (Medium space)
        steps.setBounds  (s(kOuter),                          s(cy), s(eW), s(eH));
        hits.setBounds   (s(kOuter + eW + kEucKnobGap),       s(cy), s(eW), s(eH));
        rot.setBounds    (s(kOuter + (eW + kEucKnobGap) * 2), s(cy), s(eW), s(eH));
        prePad.setBounds     (s(prePadX),                       s(cy + knobDY), s(knobW), s(knobH));
        prePadMode.setBounds (s(prePadX + knobW + kSwitchGap),  s(cy + swDY),   s(swW),   s(swH));
        postPad.setBounds    (s(postPadX),                      s(cy + knobDY), s(knobW), s(knobH));
        postPadMode.setBounds(s(postPadX + knobW + kSwitchGap), s(cy + swDY),   s(swW),   s(swH));
        insSt.setBounds  (s(insStX),  s(cy + knobDY), s(knobW), s(knobH));
        insLen.setBounds (s(insLenX), s(cy + knobDY), s(knobW), s(knobH));
        insMode.setBounds(s(insSwX),  s(cy + swDY),   s(swW),   s(swH));
    };

    int y = kOuter;
    placeRow(y, stepsA, hitsA, rotA, prePadA, postPadA, prePadModeA, postPadModeA, insertStA, insertLenA, insertModeA);

    y += rowH;
    // Logic-row layout: each sub-panel aligns vertically with the column ABOVE it —
    // Logic = Euclid knob block; Legato = Pad sub-panel; Mono = Insert sub-panel.
    // So left and right edges of the logic-row borders line up with the corresponding
    // boundaries in the Euclid row above. Vertical offset (kLogicVOffset) centres the
    // band between the Pad rects above and below.
    // The Logic dropdown is the exception: a compact control centred under the Euclid
    // knob block rather than filling it.
    {
        constexpr int legatoX = padX;
        constexpr int legatoW = padPanelW;            // matches Pad sub-panel rect above
        constexpr int monoX_  = insX;
        constexpr int monoW   = insPanelW;            // matches Insert sub-panel rect above

        logicCtrl .setBounds(s(kLogicDropX), s(y + 2 + kLogicVOffset + kLogicDropPad), s(kLogicDropW), s(kLogicDropH));
        legatoCtrl.setBounds(s(legatoX), s(y + 3 + kLogicVOffset), s(legatoW), s(kLogicH - 6));
        monoCtrl  .setBounds(s(monoX_),  s(y + 3 + kLogicVOffset), s(monoW),   s(kLogicH - 6));
    }

    y += kLogicH;
    placeRow(y, stepsB, hitsB, rotB, prePadB, postPadB, prePadModeB, postPadModeB, insertStB, insertLenB, insertModeB);

    y += rowH;
    placeRow(y, stepsC, hitsC, rotC, prePadC, postPadC, prePadModeC, postPadModeC, insertStC, insertLenC, insertModeC);
}

void EuclideanPanel::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    using mu_ui::s;

    // Match resized()'s constants exactly — see Medium-baseline values in MuLookAndFeel.
    constexpr int w      = MuLookAndFeel::kEuclidInnerW;
    constexpr int innerW = w - 2 * kOuter;
    constexpr int innerH = MuLookAndFeel::kEuclidInnerH - 2 * kOuter;
    constexpr int rowH   = (innerH - kLogicH) / 3;

    constexpr int rowOffsets[3] = { kOuter, kOuter + rowH + kLogicH, kOuter + 2 * rowH + kLogicH };
    const char* rowLabels[3]    = { "Euclid A", "Euclid B", "Accent" };

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(9.0f))));
    g.setColour(MuLookAndFeel::colour(Id::labelText));
    for (int i = 0; i < 3; ++i)
        g.drawText(rowLabels[i], s(kOuter), s(rowOffsets[i]), s(innerW), s(kLabelH), juce::Justification::centredLeft, false);

    if (rhythmColour == juce::Colours::transparentBlack)
        return;

    const juce::Colour minorCol = rhythmColour.withAlpha(0.5f);
    g.setColour(minorCol);

    // Constants mirror resized() exactly — Euclid block + Pad/Insert split.
    constexpr int eW        = MuLookAndFeel::kKnobSize1W;
    constexpr int eucBlockW = eW * 3 + kEucKnobGap * 2;
    constexpr int pW        = (innerW - eucBlockW) / 4;
    constexpr int padX      = kOuter + eucBlockW;
    constexpr int padPanelW = pW * 2 - kPadInsertGap / 2;
    constexpr int insX      = padX + pW * 2 + kPadInsertGap / 2;
    constexpr int insPanelW = w - kOuter - insX;

    constexpr int ctrlH = rowH - kLabelH;
    for (int rowY : rowOffsets)
    {
        const int cy = rowY + kLabelH;
        g.drawRoundedRectangle((float) s(padX), (float) s(cy), (float) s(padPanelW), (float) s(ctrlH) - 2.0f, 4.0f, 1.0f);
        g.drawRoundedRectangle((float) s(insX), (float) s(cy), (float) s(insPanelW), (float) s(ctrlH) - 2.0f, 4.0f, 1.0f);
    }

    {
        constexpr int rowY    = kOuter + rowH;
        // Logic-row sub-panel borders mirror the column boundaries of the Euclid row above:
        //   Logic = centred under the Steps/Hits/Rotate block; Legato = Pad rect; Mono = Insert rect.
        // Width/X values mirror the paint() Pad/Insert rect computation higher in this
        // function so left + right edges line up pixel-for-pixel across rows.
        constexpr int rectY  = rowY + 2 + kLogicVOffset;
        constexpr int rectH  = kLogicH - 4;

        g.drawRoundedRectangle((float) s(kLogicDropX - kLogicDropPad), (float) s(rectY),
                               (float) s(kLogicDropW + 2 * kLogicDropPad), (float) s(rectH), 4.0f, 1.0f);
        g.drawRoundedRectangle((float) s(padX),   (float) s(rectY),
                               (float) s(padPanelW),  (float) s(rectH), 4.0f, 1.0f);
        g.drawRoundedRectangle((float) s(insX),   (float) s(rectY),
                               (float) s(insPanelW),  (float) s(rectH), 4.0f, 1.0f);
    }
}
