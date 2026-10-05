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

    // Logic dropdown — populated with 1-based IDs that map to APVTS "logic" via id - 1.
    // Not added here: the host parents and places it (see getLogicControl).
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

    // Insert Start reads as a step number: the centred value, status bar and typed entry
    // all go through these, so they agree.
    for (auto [knob, ring] : { std::pair<KnobWithLabel*, int>{ &insertStA, 0 },
                               std::pair<KnobWithLabel*, int>{ &insertStB, 1 },
                               std::pair<KnobWithLabel*, int>{ &insertStC, 2 } })
    {
        knob->getSlider().textFromValueFunction = [this, ring](double v)
        { return juce::String((int) std::lround(v) + insertStartDisplayOffset(ring)); };
        knob->getSlider().valueFromTextFunction = [this, ring](const juce::String& t)
        { return (double) (t.getIntValue() - insertStartDisplayOffset(ring)); };
    }

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

int EuclideanPanel::insertStartDisplayOffset(int ring) const
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return 1;
    const Rhythm& r = proc.getRhythm(rhythmIndex);
    const HitGenerator& g = ring == 0 ? r.genA : ring == 1 ? r.genB : r.genC;
    const auto lay = g.clampLayout({ g.hits, g.rotate, g.prePad, g.postPad, g.insertStart, g.insertLength });
    return (g.prePadMode == InsertMode::Pad ? lay.prePad : 0) + 1;   // a Pad-mode gap precedes the section
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

juce::String EuclideanPanel::legatoExplanation(int modeIndex)
{
    return modeIndex == 1 ? "Leg - back-to-back hits carry the envelopes on, playing as one note"
                          : "Trig - every hit restarts the envelopes";
}

juce::String EuclideanPanel::monoExplanation(int modeIndex)
{
    return modeIndex == 1 ? "Mono - each hit cuts off the one before"
                          : "Poly - each hit gets its own voice, so tails overlap";
}

void EuclideanPanel::wireCallbacks()
{
    auto notify = [this] { if (onPatternChanged) onPatternChanged(); };

    // ── Euclid A ─────────────────────────────────────────────────────────────
    stepsA.onValueChanged = [this, notify](double v) {
        apvtsSet("stepsA", (float)v);
        updateRangesA();  notify();
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
        apvtsSet("prePadA", (float)v);  updateRangesA();  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Pre Pad", juce::String((int)v));
    };
    postPadA.onValueChanged = [this, notify](double v) {
        apvtsSet("postPadA", (float)v);  updateRangesA();  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Post Pad", juce::String((int)v));
    };
    insertStA.onValueChanged = [this, notify](double v) {
        apvtsSet("insStA", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Start", insertStA.getSlider().getTextFromValue(v));
    };
    insertLenA.onValueChanged = [this, notify](double v) {
        apvtsSet("insLenA", (float)v);  updateRangesA();  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Length", juce::String((int)v));
    };
    prePadModeA.onChange = [this, notify](int idx) {
        apvtsSet("prePadModeA", idx == 1 ? 1.0f : 0.0f);  updateRangesA();  notify();
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
        if (onStatusUpdate) onStatusUpdate("Pattern Legato", legatoExplanation(idx));
    };

    // Mono = polyphony cap. VoiceEngine::trigger forces voices[0] when active.
    // Independent of legato — Legato controls retrigger behaviour on the
    // single voice that exists in mono mode.
    monoCtrl.onChange = [this, notify](int idx) {
        apvtsSet("vMono", idx > 0 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Voice Mode", monoExplanation(idx));
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
        updateRangesB();  notify();
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
        apvtsSet("prePadB", (float)v);  updateRangesB();  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Pre Pad", juce::String((int)v));
    };
    postPadB.onValueChanged = [this, notify](double v) {
        apvtsSet("postPadB", (float)v);  updateRangesB();  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Post Pad", juce::String((int)v));
    };
    insertStB.onValueChanged = [this, notify](double v) {
        apvtsSet("insStB", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Start", insertStB.getSlider().getTextFromValue(v));
    };
    insertLenB.onValueChanged = [this, notify](double v) {
        apvtsSet("insLenB", (float)v);  updateRangesB();  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Length", juce::String((int)v));
    };
    prePadModeB.onChange = [this, notify](int idx) {
        apvtsSet("prePadModeB", idx == 1 ? 1.0f : 0.0f);  updateRangesB();  notify();
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
        updateRangesC();  notify();
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
        apvtsSet("prePadC", (float)v);  updateRangesC();  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Pre Pad", juce::String((int)v));
    };
    postPadC.onValueChanged = [this, notify](double v) {
        apvtsSet("postPadC", (float)v);  updateRangesC();  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Post Pad", juce::String((int)v));
    };
    insertStC.onValueChanged = [this, notify](double v) {
        apvtsSet("insStC", (float)v);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Start", insertStC.getSlider().getTextFromValue(v));
    };
    insertLenC.onValueChanged = [this, notify](double v) {
        apvtsSet("insLenC", (float)v);  updateRangesC();  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Length", juce::String((int)v));
    };
    prePadModeC.onChange = [this, notify](int idx) {
        apvtsSet("prePadModeC", idx == 1 ? 1.0f : 0.0f);  updateRangesC();  notify();
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

    updateRangesA();
    stepsA.setValue(r.genA.steps);   hitsA.setValue(r.genA.hits);
    rotA.setValue(r.genA.rotate);    prePadA.setValue(r.genA.prePad);
    postPadA.setValue(r.genA.postPad);
    insertStA.setValue(r.genA.insertStart);
    insertLenA.setValue(r.genA.insertLength);
    prePadModeA.setSelectedIndex(r.genA.prePadMode   == InsertMode::Mute ? 1 : 0);
    postPadModeA.setSelectedIndex(r.genA.postPadMode == InsertMode::Mute ? 1 : 0);
    insertModeA.setSelectedIndex(r.genA.insertMode   == InsertMode::Mute ? 1 : 0);

    updateRangesB();
    stepsB.setValue(r.genB.steps);   hitsB.setValue(r.genB.hits);
    rotB.setValue(r.genB.rotate);    prePadB.setValue(r.genB.prePad);
    postPadB.setValue(r.genB.postPad);
    insertStB.setValue(r.genB.insertStart);
    insertLenB.setValue(r.genB.insertLength);
    prePadModeB.setSelectedIndex(r.genB.prePadMode   == InsertMode::Mute ? 1 : 0);
    postPadModeB.setSelectedIndex(r.genB.postPadMode == InsertMode::Mute ? 1 : 0);
    insertModeB.setSelectedIndex(r.genB.insertMode   == InsertMode::Mute ? 1 : 0);

    updateRangesC();
    stepsC.setValue(r.genC.steps);   hitsC.setValue(r.genC.hits);
    rotC.setValue(r.genC.rotate);    prePadC.setValue(r.genC.prePad);
    postPadC.setValue(r.genC.postPad);
    insertStC.setValue(r.genC.insertStart);
    insertLenC.setValue(r.genC.insertLength);
    prePadModeC.setSelectedIndex(r.genC.prePadMode   == InsertMode::Mute ? 1 : 0);
    postPadModeC.setSelectedIndex(r.genC.postPadMode == InsertMode::Mute ? 1 : 0);
    insertModeC.setSelectedIndex(r.genC.insertMode   == InsertMode::Mute ? 1 : 0);

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

    // A ring parameter can move another knob's limits, so re-fit that ring's ranges first.
    if      (suffix.endsWithChar('A')) updateRangesA();
    else if (suffix.endsWithChar('B')) updateRangesB();
    else if (suffix.endsWithChar('C')) updateRangesC();

    // ── Ring A
    if      (suffix == "stepsA")          stepsA.setValue(r.genA.steps);
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
    else if (suffix == "stepsB")          stepsB.setValue(r.genB.steps);
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
    else if (suffix == "stepsC")          stepsC.setValue(r.genC.steps);
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

// Fit a ring's knob ranges to what its layout allows: Hits / Rotate track the step count,
// the three pads share the Steps - 1 budget, and Insert Start stays between the pads.
void EuclideanPanel::updateRanges(const HitGenerator& g, const char* const (&sfx)[4],
                                  KnobWithLabel& hits, KnobWithLabel& rot,
                                  KnobWithLabel& pre, KnobWithLabel& post,
                                  KnobWithLabel& insSt, KnobWithLabel& insLen)
{
    const int  steps  = g.steps;
    const int  budget = HitGenerator::maxPadding(steps);
    const auto lay    = g.clampLayout({ g.hits, g.rotate, g.prePad, g.postPad, g.insertStart, g.insertLength });

    hits.setRange(0, steps, 1);
    rot.setRange(0, juce::jmax(0, steps - 1), 1);   // full 0..steps-1 per design-sequencer.md
    pre.setRange   (0, juce::jmin(HitGenerator::kMaxPrePad,       budget - lay.postPad - lay.insertLength), 1);
    post.setRange  (0, juce::jmin(HitGenerator::kMaxPostPad,      budget - lay.prePad  - lay.insertLength), 1);
    insLen.setRange(0, juce::jmin(HitGenerator::kMaxInsertLength, budget - lay.prePad  - lay.postPad),      1);

    const auto [lo, hi] = HitGenerator::insertStartBounds(steps, lay.prePad, lay.postPad,
                                                          lay.insertLength, g.prePadMode);
    insSt.setRange(lo, hi, 1);
    insSt.repaint();   // its shown step number depends on the pre-pad, not just its own value

    // Write back any stored value the layout pulled in, so the parameter matches what
    // plays and what the knob shows (a range change clamps the knob silently).
    const int raw[4]    = { g.prePad,   g.postPad,   g.insertLength,   g.insertStart   };
    const int fitted[4] = { lay.prePad, lay.postPad, lay.insertLength, lay.insertStart };
    for (int i = 0; i < 4; ++i)
        if (raw[i] != fitted[i])
            apvtsSet(sfx[i], (float) fitted[i]);
}

void EuclideanPanel::updateRangesA()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    static const char* const kSfx[4] = { "prePadA", "postPadA", "insLenA", "insStA" };
    updateRanges(proc.getRhythm(rhythmIndex).genA, kSfx, hitsA, rotA, prePadA, postPadA, insertStA, insertLenA);
}

void EuclideanPanel::updateRangesB()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    static const char* const kSfx[4] = { "prePadB", "postPadB", "insLenB", "insStB" };
    updateRanges(proc.getRhythm(rhythmIndex).genB, kSfx, hitsB, rotB, prePadB, postPadB, insertStB, insertLenB);
}

void EuclideanPanel::updateRangesC()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    static const char* const kSfx[4] = { "prePadC", "postPadC", "insLenC", "insStC" };
    updateRanges(proc.getRhythm(rhythmIndex).genC, kSfx, hitsC, rotC, prePadC, postPadC, insertStC, insertLenC);
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

    // Euclid destinations: hits / rotate / insert start report 0..1 proportions; the pads
    // and insert length report actual steps, since their knob ranges follow the padding
    // budget. Arc clears when sequencer stops.
    auto bind = [&](KnobWithLabel& k, const char* destId, int snapIndex)
    {
        const juce::String id(destId);
        const bool normMode = ! (id.endsWith("Pad") || id.endsWith("insLen"));
        k.bindModulation(destId, mx,
            [&proc = proc, ri = rhythmIndex, snapIndex]() -> float {
                return proc.sequencerPlaying.load() ? proc.getModSnapshot(ri, snapIndex) : kNaN; },
            normMode);
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

    constexpr int rowH  = innerH / 3;
    constexpr int ctrlH = rowH - kLabelH;   // control zone within each row (below label)

    // Steps/Hits/Rotate render at Size 1 (canonical). kEucKnobGap
    // separates the three knobs visually; the whole block then defines where
    // the Pad sub-panel begins, so the row's right-hand columns shrink to
    // absorb the extra width.
    constexpr int eW    = MuLookAndFeel::kKnobSize1W;
    constexpr int eH    = MuLookAndFeel::kKnobSize1H;
    constexpr int eucBlockW = eW * 3 + kEucKnobGap * 2;
    constexpr int pW    = (innerW - eucBlockW - kModeColW) / 4;
    constexpr int padX  = kOuter + eucBlockW + kModeColW;
    // kPadInsertGap splits the Pad and Insert sub-panel borders so they
    // no longer share a pixel. Half the gap is taken from each side.
    constexpr int padPanelW = pW * 2 - kPadInsertGap / 2;
    constexpr int insX      = padX + pW * 2 + kPadInsertGap / 2;
    constexpr int insPanelW = MuLookAndFeel::kEuclidInnerW - kOuter - insX;

    // Pad and Insert sub-panels: Size 1 knobs, each Pad/Mute slide switch beside its
    // knob, all centred vertically in the sub-panel's box (ctrlH - 2 tall, as painted).
    constexpr int knobW  = MuLookAndFeel::kKnobSize1W;
    constexpr int knobH  = MuLookAndFeel::kKnobSize1H;
    constexpr int swW    = MuLookAndFeel::kSlideSwitchW;
    constexpr int swH    = MuLookAndFeel::kSlideSwitchH;
    constexpr int knobDY = (ctrlH - 2 - knobH) / 2;
    constexpr int swDY   = (ctrlH - 2 - swH) / 2;

    // Pad sub-panel: two [knob | switch] units side by side — at Size 1 this is within a
    // few px of the sub-panel's width, so a wider knob or switch needs the panel to grow.
    constexpr int unitW    = knobW + kSwitchGap + swW;
    constexpr int padSpan  = unitW * 2 + kSwitchGap;
    static_assert(padSpan <= padPanelW && knobH <= ctrlH - 2, "Pad knobs + switches overflow the sub-panel");
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
        const int cy  = y + kLabelH;            // top of control zone (Medium space)
        const int eCy = cy + (ctrlH - eH) / 2;  // Euclid knobs centred in it
        steps.setBounds  (s(kOuter),                          s(eCy), s(eW), s(eH));
        hits.setBounds   (s(kOuter + eW + kEucKnobGap),       s(eCy), s(eW), s(eH));
        rot.setBounds    (s(kOuter + (eW + kEucKnobGap) * 2), s(eCy), s(eW), s(eH));
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

    // Legato over Mono, centred on the panel's height in the column before the Pad sub-panel.
    {
        constexpr int colX  = kOuter + eucBlockW + kModeColPad;
        constexpr int pairY = kOuter + (innerH - (swH * 2 + kModeSwGap)) / 2;
        legatoCtrl.setBounds(s(colX), s(pairY),                    s(swW), s(swH));
        monoCtrl  .setBounds(s(colX), s(pairY + swH + kModeSwGap), s(swW), s(swH));
    }

    y += rowH;
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
    constexpr int rowH   = innerH / 3;

    constexpr int rowOffsets[3] = { kOuter, kOuter + rowH, kOuter + 2 * rowH };
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
    constexpr int pW        = (innerW - eucBlockW - kModeColW) / 4;
    constexpr int padX      = kOuter + eucBlockW + kModeColW;
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
}
