#include "EuclideanPanel.h"

EuclideanPanel::EuclideanPanel()
{
    for (auto* k : { &stepsA, &hitsA, &rotA, &prePadA, &postPadA, &insertStA, &insertLenA,
                     &stepsB, &hitsB, &rotB, &prePadB, &postPadB, &insertStB, &insertLenB,
                     &stepsC, &hitsC, &rotC, &prePadC, &postPadC, &insertStC, &insertLenC })
        addAndMakeVisible(k);

    for (auto* s : { &insertModeA, &logicCtrl, &insertModeB, &insertModeC })
        addAndMakeVisible(s);

    // Ranges — steps max 64 per user spec
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

void EuclideanPanel::wireCallbacks()
{
    auto notify = [this] { if (onPatternChanged) onPatternChanged(); };

    // ── Euclid A ─────────────────────────────────────────────────────────────
    stepsA.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genA.steps = (int)v;
        updateRangesA(rhythm->genA.steps);
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Steps", juce::String((int)v));
    };
    hitsA.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genA.hits = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Hits", juce::String((int)v));
    };
    rotA.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genA.rotate = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Rotate", juce::String((int)v));
    };
    prePadA.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genA.prePad = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Pre Pad", juce::String((int)v));
    };
    postPadA.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genA.postPad = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Post Pad", juce::String((int)v));
    };
    insertStA.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genA.insertStart = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Start", juce::String((int)v));
    };
    insertLenA.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genA.insertLength = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid A Insert Length", juce::String((int)v));
    };
    insertModeA.onChange = [this, notify](int idx) {
        if (!rhythm) return;
        rhythm->genA.insertMode = (idx == 1) ? InsertMode::Mute : InsertMode::Pad;
        notify();
    };

    // ── Logic ─────────────────────────────────────────────────────────────────
    logicCtrl.onChange = [this, notify](int idx) {
        if (!rhythm) return;
        static const Logic logics[] = { Logic::OR, Logic::AND, Logic::XOR,
                                        Logic::AOnly, Logic::BOnly };
        rhythm->logic = logics[idx];
        notify();
    };

    // ── Euclid B ─────────────────────────────────────────────────────────────
    stepsB.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genB.steps = (int)v;
        updateRangesB(rhythm->genB.steps);
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Steps", juce::String((int)v));
    };
    hitsB.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genB.hits = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Hits", juce::String((int)v));
    };
    rotB.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genB.rotate = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Rotate", juce::String((int)v));
    };
    prePadB.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genB.prePad = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Pre Pad", juce::String((int)v));
    };
    postPadB.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genB.postPad = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Post Pad", juce::String((int)v));
    };
    insertStB.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genB.insertStart = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Start", juce::String((int)v));
    };
    insertLenB.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genB.insertLength = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Euclid B Insert Length", juce::String((int)v));
    };
    insertModeB.onChange = [this, notify](int idx) {
        if (!rhythm) return;
        rhythm->genB.insertMode = (idx == 1) ? InsertMode::Mute : InsertMode::Pad;
        notify();
    };

    // ── Euclid C (Accent) ─────────────────────────────────────────────────────
    stepsC.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genC.steps = (int)v;
        updateRangesC(rhythm->genC.steps);
        notify();
        if (onStatusUpdate) onStatusUpdate("Accent Steps", juce::String((int)v));
    };
    hitsC.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genC.hits = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Accent Hits", juce::String((int)v));
    };
    rotC.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genC.rotate = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Accent Rotate", juce::String((int)v));
    };
    prePadC.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genC.prePad = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Accent Pre Pad", juce::String((int)v));
    };
    postPadC.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genC.postPad = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Accent Post Pad", juce::String((int)v));
    };
    insertStC.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genC.insertStart = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Start", juce::String((int)v));
    };
    insertLenC.onValueChanged = [this, notify](double v) {
        if (!rhythm) return;
        rhythm->genC.insertLength = (int)v;
        notify();
        if (onStatusUpdate) onStatusUpdate("Accent Insert Length", juce::String((int)v));
    };
    insertModeC.onChange = [this, notify](int idx) {
        if (!rhythm) return;
        rhythm->genC.insertMode = (idx == 1) ? InsertMode::Mute : InsertMode::Pad;
        notify();
    };
}

void EuclideanPanel::setRhythm(Rhythm* r)
{
    rhythm = r;
    loadFromRhythm();
}

void EuclideanPanel::loadFromRhythm()
{
    if (!rhythm) return;

    stepsA.setValue(rhythm->genA.steps);   hitsA.setValue(rhythm->genA.hits);
    rotA.setValue(rhythm->genA.rotate);    prePadA.setValue(rhythm->genA.prePad);
    postPadA.setValue(rhythm->genA.postPad);
    insertStA.setValue(rhythm->genA.insertStart);
    insertLenA.setValue(rhythm->genA.insertLength);
    insertModeA.setSelectedIndex(rhythm->genA.insertMode == InsertMode::Mute ? 1 : 0);
    updateRangesA(rhythm->genA.steps);

    stepsB.setValue(rhythm->genB.steps);   hitsB.setValue(rhythm->genB.hits);
    rotB.setValue(rhythm->genB.rotate);    prePadB.setValue(rhythm->genB.prePad);
    postPadB.setValue(rhythm->genB.postPad);
    insertStB.setValue(rhythm->genB.insertStart);
    insertLenB.setValue(rhythm->genB.insertLength);
    insertModeB.setSelectedIndex(rhythm->genB.insertMode == InsertMode::Mute ? 1 : 0);
    updateRangesB(rhythm->genB.steps);

    stepsC.setValue(rhythm->genC.steps);   hitsC.setValue(rhythm->genC.hits);
    rotC.setValue(rhythm->genC.rotate);    prePadC.setValue(rhythm->genC.prePad);
    postPadC.setValue(rhythm->genC.postPad);
    insertStC.setValue(rhythm->genC.insertStart);
    insertLenC.setValue(rhythm->genC.insertLength);
    insertModeC.setSelectedIndex(rhythm->genC.insertMode == InsertMode::Mute ? 1 : 0);
    updateRangesC(rhythm->genC.steps);

    static const Logic logics[] = { Logic::OR, Logic::AND, Logic::XOR,
                                    Logic::AOnly, Logic::BOnly };
    for (int i = 0; i < 5; i++)
        if (rhythm->logic == logics[i]) { logicCtrl.setSelectedIndex(i); break; }
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

void EuclideanPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    const int rowH = juce::jmin(kMaxRowH, (h - kLogicH) / 3);
    const int colW = w / 8;  // 8 controls per row

    // M/I switch height — sits at the bottom of column 7, under the knob graphic
    const int modeH = juce::jmin(20, rowH / 3);

    // ── Euclid A (single row) ────────────────────────────────────────────────
    int y = 0;
    stepsA.setBounds    (colW * 0, y, colW,         rowH);
    hitsA.setBounds     (colW * 1, y, colW,         rowH);
    rotA.setBounds      (colW * 2, y, colW,         rowH);
    prePadA.setBounds   (colW * 3, y, colW,         rowH);
    postPadA.setBounds  (colW * 4, y, colW,         rowH);
    insertStA.setBounds (colW * 5, y, colW,         rowH);
    insertLenA.setBounds(colW * 6, y, colW,         rowH);
    insertModeA.setBounds(colW * 7, y + rowH - modeH, w - colW * 7, modeH);

    // ── Logic bar ─────────────────────────────────────────────────────────────
    y += rowH;
    logicCtrl.setBounds(0, y, w, kLogicH);

    // ── Euclid B (single row) ────────────────────────────────────────────────
    y += kLogicH;
    stepsB.setBounds    (colW * 0, y, colW,         rowH);
    hitsB.setBounds     (colW * 1, y, colW,         rowH);
    rotB.setBounds      (colW * 2, y, colW,         rowH);
    prePadB.setBounds   (colW * 3, y, colW,         rowH);
    postPadB.setBounds  (colW * 4, y, colW,         rowH);
    insertStB.setBounds (colW * 5, y, colW,         rowH);
    insertLenB.setBounds(colW * 6, y, colW,         rowH);
    insertModeB.setBounds(colW * 7, y + rowH - modeH, w - colW * 7, modeH);

    // ── Euclid C / Accent (single row, same 8 controls) ──────────────────────
    y += rowH;
    stepsC.setBounds    (colW * 0, y, colW,         rowH);
    hitsC.setBounds     (colW * 1, y, colW,         rowH);
    rotC.setBounds      (colW * 2, y, colW,         rowH);
    prePadC.setBounds   (colW * 3, y, colW,         rowH);
    postPadC.setBounds  (colW * 4, y, colW,         rowH);
    insertStC.setBounds (colW * 5, y, colW,         rowH);
    insertLenC.setBounds(colW * 6, y, colW,         rowH);
    insertModeC.setBounds(colW * 7, y + rowH - modeH, w - colW * 7, modeH);
}

void EuclideanPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int rowH = juce::jmin(kMaxRowH, (getHeight() - kLogicH) / 3);
    g.setFont(juce::Font(8.0f));
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.drawText("A", 0, 0,                    10, rowH, juce::Justification::centredTop, false);
    g.drawText("B", 0, rowH + kLogicH,       10, rowH, juce::Justification::centredTop, false);
    g.drawText("C", 0, 2 * rowH + kLogicH,   10, rowH, juce::Justification::centredTop, false);
}
