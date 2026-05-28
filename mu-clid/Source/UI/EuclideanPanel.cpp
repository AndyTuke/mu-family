#include "EuclideanPanel.h"
#include "Plugin/PluginProcessor.h"
#include <limits>

EuclideanPanel::EuclideanPanel(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &stepsA, &hitsA, &rotA, &prePadA, &postPadA, &insertStA, &insertLenA,
                     &stepsB, &hitsB, &rotB, &prePadB, &postPadB, &insertStB, &insertLenB,
                     &stepsC, &hitsC, &rotC, &prePadC, &postPadC, &insertStC, &insertLenC })
        addAndMakeVisible(k);

    for (auto* s : { &prePadModeA, &postPadModeA, &insertModeA,
                     &legatoCtrl, &monoCtrl,
                     &prePadModeB, &postPadModeB, &insertModeB,
                     &prePadModeC, &postPadModeC, &insertModeC })
        addAndMakeVisible(s);

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
    // status-bar coverage for segment toggles — mode text matches dropdown.
    auto padModeLabel = [](int idx) { return idx == 1 ? juce::String("Mute") : juce::String("Pad"); };
    prePadModeA.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("prePadModeA", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Pre Pad Mode", padModeLabel(idx));
    };
    postPadModeA.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("postPadModeA", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Post Pad Mode", padModeLabel(idx));
    };
    insertModeA.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("insModeA", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Mode", padModeLabel(idx));
    };

    // ── Legato (#419) ─────────────────────────────────────────────────────────
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
    prePadModeB.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("prePadModeB", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Pre Pad Mode", padModeLabel(idx));
    };
    postPadModeB.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("postPadModeB", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Post Pad Mode", padModeLabel(idx));
    };
    insertModeB.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("insModeB", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Mode", padModeLabel(idx));
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
    prePadModeC.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("prePadModeC", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Pre Pad Mode", padModeLabel(idx));
    };
    postPadModeC.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("postPadModeC", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Post Pad Mode", padModeLabel(idx));
    };
    insertModeC.onChange = [this, notify, padModeLabel](int idx) {
        apvtsSet("insModeC", idx == 1 ? 1.0f : 0.0f);  notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Mode", padModeLabel(idx));
    };
}

void EuclideanPanel::setRhythm(int ri)
{
    if (ri != rhythmIndex)
        paramPtrCache.clear();   // cached pointers were keyed to the previous rhythmIndex's IDs
    rhythmIndex = ri;
    loadFromRhythm();
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

    auto setMode = [](SegmentControl& sc, InsertMode m) {
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

void EuclideanPanel::refreshModulatedIndicators()
{
    if (rhythmIndex < 0 || rhythmIndex >= proc.getNumRhythms()) return;
    const auto& assigns = proc.getRhythm(rhythmIndex).modulationMatrix.getAssignments();

    auto isAssigned = [&assigns](const char* destId) -> bool
    {
        const std::string s = destId;
        for (const auto& a : assigns)
            if (a.destinationId == s) return true;
        return false;
    };

    // Only show ring + live arc while playing — when stopped the snapshot holds the
    // last played position, which would read as a permanent misleading indicator.
    const bool playing = proc.sequencerPlaying.load();

    hitsA    .setIsModulated(playing && isAssigned("euclid.a.hits"));
    rotA     .setIsModulated(playing && isAssigned("euclid.a.rotate"));
    prePadA  .setIsModulated(playing && isAssigned("euclid.a.prePad"));
    postPadA .setIsModulated(playing && isAssigned("euclid.a.postPad"));
    insertStA.setIsModulated(playing && isAssigned("euclid.a.insSt"));
    insertLenA.setIsModulated(playing && isAssigned("euclid.a.insLen"));

    hitsB    .setIsModulated(playing && isAssigned("euclid.b.hits"));
    rotB     .setIsModulated(playing && isAssigned("euclid.b.rotate"));
    prePadB  .setIsModulated(playing && isAssigned("euclid.b.prePad"));
    postPadB .setIsModulated(playing && isAssigned("euclid.b.postPad"));
    insertStB.setIsModulated(playing && isAssigned("euclid.b.insSt"));
    insertLenB.setIsModulated(playing && isAssigned("euclid.b.insLen"));

    hitsC    .setIsModulated(playing && isAssigned("euclid.c.hits"));
    rotC     .setIsModulated(playing && isAssigned("euclid.c.rotate"));
    prePadC  .setIsModulated(playing && isAssigned("euclid.c.prePad"));
    postPadC .setIsModulated(playing && isAssigned("euclid.c.postPad"));
    insertStC.setIsModulated(playing && isAssigned("euclid.c.insSt"));
    insertLenC.setIsModulated(playing && isAssigned("euclid.c.insLen"));

    // Stage C: live-arc indicator values from the modulation snapshot.
    auto sn  = [&](int i) { return proc.getModSnapshot(rhythmIndex, i); };
    const float kNaN = std::numeric_limits<float>::quiet_NaN();
    auto arc = [&](bool assigned, int idx) { return (assigned && playing) ? sn(idx) : kNaN; };

    hitsA     .setModulatedNorm(arc(isAssigned("euclid.a.hits"),    kSnapEucAHits));
    rotA      .setModulatedNorm(arc(isAssigned("euclid.a.rotate"),  kSnapEucARotate));
    prePadA   .setModulatedNorm(arc(isAssigned("euclid.a.prePad"),  kSnapEucAPrePad));
    postPadA  .setModulatedNorm(arc(isAssigned("euclid.a.postPad"), kSnapEucAPostPad));
    insertStA .setModulatedNorm(arc(isAssigned("euclid.a.insSt"),   kSnapEucAInsSt));
    insertLenA.setModulatedNorm(arc(isAssigned("euclid.a.insLen"),  kSnapEucAInsLen));

    hitsB     .setModulatedNorm(arc(isAssigned("euclid.b.hits"),    kSnapEucBHits));
    rotB      .setModulatedNorm(arc(isAssigned("euclid.b.rotate"),  kSnapEucBRotate));
    prePadB   .setModulatedNorm(arc(isAssigned("euclid.b.prePad"),  kSnapEucBPrePad));
    postPadB  .setModulatedNorm(arc(isAssigned("euclid.b.postPad"), kSnapEucBPostPad));
    insertStB .setModulatedNorm(arc(isAssigned("euclid.b.insSt"),   kSnapEucBInsSt));
    insertLenB.setModulatedNorm(arc(isAssigned("euclid.b.insLen"),  kSnapEucBInsLen));

    hitsC     .setModulatedNorm(arc(isAssigned("euclid.c.hits"),    kSnapEucCHits));
    rotC      .setModulatedNorm(arc(isAssigned("euclid.c.rotate"),  kSnapEucCRotate));
    prePadC   .setModulatedNorm(arc(isAssigned("euclid.c.prePad"),  kSnapEucCPrePad));
    postPadC  .setModulatedNorm(arc(isAssigned("euclid.c.postPad"), kSnapEucCPostPad));
    insertStC .setModulatedNorm(arc(isAssigned("euclid.c.insSt"),   kSnapEucCInsSt));
    insertLenC.setModulatedNorm(arc(isAssigned("euclid.c.insLen"),  kSnapEucCInsLen));
}

void EuclideanPanel::resized()
{
    // Fixed Medium-baseline layout — see MuLookAndFeel for the constants.
    constexpr int innerW = MuLookAndFeel::kEuclidInnerW - 2 * kOuter;   // panel width minus its 4 px border
    constexpr int innerH = MuLookAndFeel::kEuclidInnerH - 2 * kOuter;

    constexpr int rowH  = (innerH - kLogicH) / 3;
    constexpr int ctrlH = rowH - kLabelH;   // control zone within each row (below label)
    constexpr int mP    = 4;

    // Steps/Hits/Rotate render at Size 1 (canonical). #619 — kEucKnobGap
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

    constexpr int knobH    = ctrlH - kSwitchH - 6;
    // Pad/Mute toggle width — fits the longest text ("MUTE") cleanly.
    constexpr int insSw    = (pW < 56) ? pW : 56;
    constexpr int insSwX   = insX + (insPanelW - insSw) / 2;

    // Pad knob pair: two Size-3 knobs centred inside the Pad sub-panel with
    // kPadKnobGap between them. Mute toggles centre under each knob; padSw is
    // capped so the two toggles never overlap.
    constexpr int padKnobW      = MuLookAndFeel::kKnobSize3W;
    constexpr int padKnobH      = MuLookAndFeel::kKnobSize3H;
    constexpr int padPairW      = padKnobW * 2 + kPadKnobGap;
    constexpr int prePadX       = padX + (padPanelW - padPairW) / 2;
    constexpr int postPadX      = prePadX + padKnobW + kPadKnobGap;
    constexpr int padSwMax      = (padPairW - 4) / 2;
    constexpr int padSw         = padSwMax < 56 ? padSwMax : 56;
    constexpr int preSw_x       = prePadX  + (padKnobW - padSw) / 2;
    constexpr int postSw_x      = postPadX + (padKnobW - padSw) / 2;

    // Insert pair: centred inside the Insert sub-panel using the SAME pair
    // geometry as the Pad pair (padPairW + kPadKnobGap), so the Pre/Post Pad
    // spacing and the Insert spacing are identical. Previously the two insert
    // knobs sat one-per-pW-column (~104 px apart) — far wider than the Pad pair.
    constexpr int insStX  = insX + (insPanelW - padPairW) / 2;
    constexpr int insLenX = insStX + padKnobW + kPadKnobGap;

    // Every literal/constant in setBounds wrapped in mu_ui::s() so toggling
    // the UI scale propagates uniformly. Identity at scale = 1.0.
    using mu_ui::s;

    auto placeRow = [&](int y,
                        KnobWithLabel& steps, KnobWithLabel& hits, KnobWithLabel& rot,
                        KnobWithLabel& prePad, KnobWithLabel& postPad,
                        SegmentControl& prePadMode, SegmentControl& postPadMode,
                        KnobWithLabel& insSt, KnobWithLabel& insLen,
                        SegmentControl& insMode)
    {
        const int cy = y + kLabelH;  // top of control zone (Medium space)
        steps.setBounds  (s(kOuter),                          s(cy), s(eW), s(eH));
        hits.setBounds   (s(kOuter + eW + kEucKnobGap),       s(cy), s(eW), s(eH));
        rot.setBounds    (s(kOuter + (eW + kEucKnobGap) * 2), s(cy), s(eW), s(eH));
        prePad.setBounds (s(prePadX),  s(cy + mP), s(padKnobW), s(padKnobH));
        postPad.setBounds(s(postPadX), s(cy + mP), s(padKnobW), s(padKnobH));
        prePadMode.setBounds (s(preSw_x),  s(cy + knobH + 2), s(padSw), s(kSwitchH));
        postPadMode.setBounds(s(postSw_x), s(cy + knobH + 2), s(padSw), s(kSwitchH));
        insSt.setBounds  (s(insStX),  s(cy + mP), s(padKnobW), s(padKnobH));
        insLen.setBounds (s(insLenX), s(cy + mP), s(padKnobW), s(padKnobH));
        insMode.setBounds(s(insSwX),       s(cy + knobH + 2), s(insSw), s(kSwitchH));
    };

    int y = kOuter;
    placeRow(y, stepsA, hitsA, rotA, prePadA, postPadA, prePadModeA, postPadModeA, insertStA, insertLenA, insertModeA);

    y += rowH;
    // Logic-row layout: each sub-panel aligns vertically with the column ABOVE it —
    // Logic = Euclid knob block; Legato = Pad sub-panel; Mono = Insert sub-panel.
    // So left and right edges of the logic-row borders line up with the corresponding
    // boundaries in the Euclid row above. Vertical offset (kLogicVOffset) centres the
    // band between the Pad rects above and below.
    // Logic width shrinks by kPadInsertGap so there's a visible gap to the Legato
    // sub-panel on its right (Legato keeps the padX anchor to stay aligned with the
    // Pad rect above; gap = same width as the Pad/Insert gap to keep visual rhythm).
    {
        constexpr int logicX  = kOuter;
        constexpr int logicW  = eucBlockW - kPadInsertGap;  // shrink right edge to create gap
        constexpr int legatoX = padX;
        constexpr int legatoW = padPanelW;            // matches Pad sub-panel rect above
        constexpr int monoX_  = insX;
        constexpr int monoW   = insPanelW;            // matches Insert sub-panel rect above

        logicCtrl .setBounds(s(logicX),  s(y + 3 + kLogicVOffset), s(logicW),  s(kLogicH - 6));
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
    using Id = MuClidLookAndFeel::ColourIds;
    using mu_ui::s;

    // Match resized()'s constants exactly — see Medium-baseline values in MuLookAndFeel.
    constexpr int w      = MuLookAndFeel::kEuclidInnerW;
    constexpr int innerW = w - 2 * kOuter;
    constexpr int innerH = MuLookAndFeel::kEuclidInnerH - 2 * kOuter;
    constexpr int rowH   = (innerH - kLogicH) / 3;

    constexpr int rowOffsets[3] = { kOuter, kOuter + rowH + kLogicH, kOuter + 2 * rowH + kLogicH };
    const char* rowLabels[3]    = { "Euclid A", "Euclid B", "Accent" };

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(mu_ui::sf(9.0f))));
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    for (int i = 0; i < 3; ++i)
        g.drawText(rowLabels[i], s(kOuter), s(rowOffsets[i]), s(innerW), s(kLabelH), juce::Justification::centredLeft, false);

    if (rhythmColour == juce::Colours::transparentBlack)
        return;

    const juce::Colour minorCol = rhythmColour.withAlpha(0.5f);
    g.setColour(minorCol);

    // Constants mirror resized() exactly — Euclid block + Pad/Insert split (#618-#620).
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
        //   Logic = Steps/Hits/Rotate block; Legato = Pad rect; Mono = Insert rect.
        // Width/X values mirror the paint() Pad/Insert rect computation higher in this
        // function so left + right edges line up pixel-for-pixel across rows.
        constexpr int rectY  = rowY + 2 + kLogicVOffset;
        constexpr int rectH  = kLogicH - 4;

        g.drawRoundedRectangle((float) s(kOuter), (float) s(rectY),
                               (float) s(eucBlockW - kPadInsertGap),  (float) s(rectH), 4.0f, 1.0f);
        g.drawRoundedRectangle((float) s(padX),   (float) s(rectY),
                               (float) s(padPanelW),  (float) s(rectH), 4.0f, 1.0f);
        g.drawRoundedRectangle((float) s(insX),   (float) s(rectY),
                               (float) s(insPanelW),  (float) s(rectH), 4.0f, 1.0f);
    }
}
