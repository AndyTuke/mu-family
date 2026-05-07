#include "EuclideanPanel.h"
#include "../PluginProcessor.h"

EuclideanPanel::EuclideanPanel(PluginProcessor& p) : proc(p)
{
    for (auto* k : { &stepsA, &hitsA, &rotA, &prePadA, &postPadA, &insertStA, &insertLenA,
                     &stepsB, &hitsB, &rotB, &prePadB, &postPadB, &insertStB, &insertLenB,
                     &stepsC, &hitsC, &rotC, &prePadC, &postPadC, &insertStC, &insertLenC })
        addAndMakeVisible(k);

    for (auto* s : { &prePadModeA, &postPadModeA, &insertModeA,
                     &logicCtrl,
                     &prePadModeB, &postPadModeB, &insertModeB,
                     &prePadModeC, &postPadModeC, &insertModeC })
        addAndMakeVisible(s);

    stepsA.setRange(1, 64, 1);      hitsA.setRange(0, 64, 1);   rotA.setRange(-32, 32, 1);
    prePadA.setRange(0, 12, 1);     postPadA.setRange(0, 12, 1);
    insertStA.setRange(0, 63, 1);   insertLenA.setRange(0, 8, 1);

    stepsB.setRange(1, 64, 1);      hitsB.setRange(0, 64, 1);   rotB.setRange(-32, 32, 1);
    prePadB.setRange(0, 12, 1);     postPadB.setRange(0, 12, 1);
    insertStB.setRange(0, 63, 1);   insertLenB.setRange(0, 8, 1);

    stepsC.setRange(1, 64, 1);      hitsC.setRange(0, 64, 1);   rotC.setRange(-32, 32, 1);
    prePadC.setRange(0, 12, 1);     postPadC.setRange(0, 12, 1);
    insertStC.setRange(0, 63, 1);   insertLenC.setRange(0, 8, 1);

    stepsA.setValue(8); stepsB.setValue(8); stepsC.setValue(8);

    wireCallbacks();
}

void EuclideanPanel::apvtsSet(const char* suffix, float v)
{
    if (rhythmIndex < 0) return;
    const auto id = "r" + juce::String(rhythmIndex) + "_" + suffix;
    if (auto* p = proc.apvts.getParameter(id))
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
    prePadModeA.onChange = [this, notify](int idx) {
        apvtsSet("prePadModeA", idx == 1 ? 1.0f : 0.0f);  notify();
    };
    postPadModeA.onChange = [this, notify](int idx) {
        apvtsSet("postPadModeA", idx == 1 ? 1.0f : 0.0f);  notify();
    };
    insertModeA.onChange = [this, notify](int idx) {
        apvtsSet("insModeA", idx == 1 ? 1.0f : 0.0f);  notify();
    };

    // ── Logic ─────────────────────────────────────────────────────────────────
    logicCtrl.onChange = [this, notify](int idx) {
        apvtsSet("logic", (float)idx);  notify();
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
    };
    postPadModeB.onChange = [this, notify](int idx) {
        apvtsSet("postPadModeB", idx == 1 ? 1.0f : 0.0f);  notify();
    };
    insertModeB.onChange = [this, notify](int idx) {
        apvtsSet("insModeB", idx == 1 ? 1.0f : 0.0f);  notify();
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
    };
    postPadModeC.onChange = [this, notify](int idx) {
        apvtsSet("postPadModeC", idx == 1 ? 1.0f : 0.0f);  notify();
    };
    insertModeC.onChange = [this, notify](int idx) {
        apvtsSet("insModeC", idx == 1 ? 1.0f : 0.0f);  notify();
    };
}

void EuclideanPanel::setRhythm(int ri)
{
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
        if (r.logic == logics[i]) { logicCtrl.setSelectedIndex(i); break; }
}

void EuclideanPanel::updateRangesA(int steps)
{
    hitsA.setRange(0, steps, 1);
    rotA.setRange(-(steps / 2), steps / 2, 1);
    insertStA.setRange(0, juce::jmax(0, steps - 1), 1);
}

void EuclideanPanel::updateRangesB(int steps)
{
    hitsB.setRange(0, steps, 1);
    rotB.setRange(-(steps / 2), steps / 2, 1);
    insertStB.setRange(0, juce::jmax(0, steps - 1), 1);
}

void EuclideanPanel::updateRangesC(int steps)
{
    hitsC.setRange(0, steps, 1);
    rotC.setRange(-(steps / 2), steps / 2, 1);
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

    hitsA.setIsModulated(isAssigned("euclid.a.hits"));
    rotA .setIsModulated(isAssigned("euclid.a.rotate"));
    hitsB.setIsModulated(isAssigned("euclid.b.hits"));
    rotB .setIsModulated(isAssigned("euclid.b.rotate"));
}

void EuclideanPanel::resized()
{
    const int w      = getWidth();
    const int h      = getHeight();
    const int innerW = w - 2 * kOuter;
    const int innerH = h - 2 * kOuter;

    const int rowH = (innerH - kLogicH) / 3;
    const int ctrlH = rowH - kLabelH;   // control zone within each row (below label)
    const int mP   = 4;

    const int colW = innerW / 7;
    const int eW   = (int)(colW * 0.9f);
    const int pW   = (innerW - eW * 3) / 4;
    const int padX = kOuter + eW * 3;
    const int insX = padX + pW * 2;

    const int knobH    = ctrlH - kSwitchH - 2;
    // Issue #48: widen Pad/Mute toggles 40→56 so the full text fits.
    const int insSw    = juce::jmin(56, pW);
    const int insSwX   = insX + (pW * 2 - insSw) / 2;
    const int padSw    = juce::jmin(56, pW - mP);
    const int preSw_x  = padX + mP + (pW - mP - padSw) / 2;
    const int postSw_x = padX + pW + (pW - mP - padSw) / 2;

    auto placeRow = [&](int y,
                        KnobWithLabel& steps, KnobWithLabel& hits, KnobWithLabel& rot,
                        KnobWithLabel& prePad, KnobWithLabel& postPad,
                        SegmentControl& prePadMode, SegmentControl& postPadMode,
                        KnobWithLabel& insSt, KnobWithLabel& insLen,
                        SegmentControl& insMode)
    {
        const int cy = y + kLabelH;  // top of control zone
        steps.setBounds  (kOuter,        cy,      eW,      ctrlH);
        hits.setBounds   (kOuter + eW,   cy,      eW,      ctrlH);
        rot.setBounds    (kOuter + eW*2, cy,      eW,      ctrlH);
        prePad.setBounds (padX + mP,     cy + mP, pW - mP, knobH - mP);
        postPad.setBounds(padX + pW,     cy + mP, pW - mP, knobH - mP);
        prePadMode.setBounds (preSw_x,   cy + knobH + 2, padSw, kSwitchH);
        postPadMode.setBounds(postSw_x,  cy + knobH + 2, padSw, kSwitchH);
        insSt.setBounds  (insX + mP,     cy + mP, pW,      knobH - mP);
        insLen.setBounds (insX + pW,     cy + mP, pW - mP, knobH - mP);
        insMode.setBounds(insSwX,        cy + knobH + 2, insSw, kSwitchH);
    };

    int y = kOuter;
    placeRow(y, stepsA, hitsA, rotA, prePadA, postPadA, prePadModeA, postPadModeA, insertStA, insertLenA, insertModeA);

    y += rowH;
    logicCtrl.setBounds(kOuter + mP, y + 1, innerW - mP * 2, kLogicH - 2);

    y += kLogicH;
    placeRow(y, stepsB, hitsB, rotB, prePadB, postPadB, prePadModeB, postPadModeB, insertStB, insertLenB, insertModeB);

    y += rowH;
    placeRow(y, stepsC, hitsC, rotC, prePadC, postPadC, prePadModeC, postPadModeC, insertStC, insertLenC, insertModeC);
}

void EuclideanPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int w      = getWidth();
    const int h      = getHeight();
    const int innerW = w - 2 * kOuter;
    const int innerH = h - 2 * kOuter;
    const int rowH   = (innerH - kLogicH) / 3;

    const int rowOffsets[3] = { kOuter, kOuter + rowH + kLogicH, kOuter + 2 * rowH + kLogicH };
    const char* rowLabels[3] = { "Euclid A", "Euclid B", "Accent" };

    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    for (int i = 0; i < 3; ++i)
        g.drawText(rowLabels[i], kOuter, rowOffsets[i], innerW, kLabelH, juce::Justification::centredLeft, false);

    if (rhythmColour == juce::Colours::transparentBlack)
        return;

    const juce::Colour minorCol = rhythmColour.withAlpha(0.5f);
    g.setColour(minorCol);

    const int colW = innerW / 7;
    const int eW   = (int)(colW * 0.9f);
    const int pW   = (innerW - eW * 3) / 4;
    const int padX = kOuter + eW * 3;
    const int insX = padX + pW * 2;

    const int ctrlH = rowH - kLabelH;
    for (int rowY : rowOffsets)
    {
        const int cy = rowY + kLabelH;
        g.drawRoundedRectangle((float)padX, (float)cy, (float)(pW * 2),             (float)ctrlH, 4.0f, 1.0f);
        g.drawRoundedRectangle((float)insX, (float)cy, (float)(w - kOuter - insX),  (float)ctrlH, 4.0f, 1.0f);
    }

    g.drawRoundedRectangle((float)kOuter, (float)(kOuter + rowH), (float)innerW, (float)kLogicH, 4.0f, 1.0f);
}
