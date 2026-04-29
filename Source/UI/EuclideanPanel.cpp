#include "EuclideanPanel.h"

EuclideanPanel::EuclideanPanel()
{
    for (auto* k : { &stepsA, &hitsA, &rotA, &prePadA, &postPadA, &insertStA, &insertLenA,
                     &stepsB, &hitsB, &rotB, &prePadB, &postPadB, &insertStB, &insertLenB })
        addAndMakeVisible(k);

    for (auto* s : { &insertModeA, &logicCtrl, &insertModeB })
        addAndMakeVisible(s);

    // Ranges — steps max 64 per user spec
    stepsA.setRange(1, 64, 1);      hitsA.setRange(0, 64, 1);   rotA.setRange(-32, 32, 1);
    prePadA.setRange(0, 12, 1);     postPadA.setRange(0, 12, 1);
    insertStA.setRange(0, 63, 1);   insertLenA.setRange(0, 8, 1);

    stepsB.setRange(1, 64, 1);      hitsB.setRange(0, 64, 1);   rotB.setRange(-32, 32, 1);
    prePadB.setRange(0, 12, 1);     postPadB.setRange(0, 12, 1);
    insertStB.setRange(0, 63, 1);   insertLenB.setRange(0, 8, 1);

    stepsA.setValue(8); stepsB.setValue(8);

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

    stepsB.setValue(rhythm->genB.steps);   hitsB.setValue(rhythm->genB.hits);
    rotB.setValue(rhythm->genB.rotate);    prePadB.setValue(rhythm->genB.prePad);
    postPadB.setValue(rhythm->genB.postPad);
    insertStB.setValue(rhythm->genB.insertStart);
    insertLenB.setValue(rhythm->genB.insertLength);
    insertModeB.setSelectedIndex(rhythm->genB.insertMode == InsertMode::Mute ? 1 : 0);

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

void EuclideanPanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // 4 knob rows + 20px logic bar. Cap row height for compact feel.
    const int rowH = juce::jmin(kMaxRowH, (h - kLogicH) / 4);
    const int colW = w / 4;

    // ── Euclid A Row 1 ───────────────────────────────────────────────────────
    int y = 0;
    stepsA.setBounds(0,      y, colW,          rowH);
    hitsA.setBounds (colW,   y, colW,          rowH);
    rotA.setBounds  (colW*2, y, colW,          rowH);
    prePadA.setBounds(colW*3,y, w - colW * 3,  rowH);

    // ── Euclid A Row 2 ───────────────────────────────────────────────────────
    y += rowH;
    postPadA.setBounds(0,      y, colW,         rowH);
    insertStA.setBounds(colW,  y, colW,         rowH);
    insertLenA.setBounds(colW*2,y,colW,         rowH);
    // InsertMode toggle centred vertically in its cell
    {
        const int modeH = juce::jmin(22, rowH - 8);
        const int modeY = y + (rowH - modeH) / 2;
        insertModeA.setBounds(colW * 3, modeY, w - colW * 3, modeH);
    }

    // ── Logic bar ─────────────────────────────────────────────────────────────
    y += rowH;
    logicCtrl.setBounds(0, y, w, kLogicH);

    // ── Euclid B Row 1 ───────────────────────────────────────────────────────
    y += kLogicH;
    stepsB.setBounds(0,      y, colW,          rowH);
    hitsB.setBounds (colW,   y, colW,          rowH);
    rotB.setBounds  (colW*2, y, colW,          rowH);
    prePadB.setBounds(colW*3,y, w - colW * 3,  rowH);

    // ── Euclid B Row 2 ───────────────────────────────────────────────────────
    y += rowH;
    postPadB.setBounds(0,      y, colW,         rowH);
    insertStB.setBounds(colW,  y, colW,         rowH);
    insertLenB.setBounds(colW*2,y,colW,         rowH);
    {
        const int modeH = juce::jmin(22, rowH - 8);
        const int modeY = y + (rowH - modeH) / 2;
        insertModeB.setBounds(colW * 3, modeY, w - colW * 3, modeH);
    }
}

void EuclideanPanel::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const int rowH  = juce::jmin(kMaxRowH, (getHeight() - kLogicH) / 4);
    const int aLabelY = 0;
    const int bLabelY = rowH * 2 + kLogicH;

    g.setFont(juce::Font(8.0f));
    g.setColour(MuClidLookAndFeel::colour(Id::mutedText));
    g.drawText("A", 0, aLabelY, 10, rowH, juce::Justification::centredTop, false);
    g.drawText("B", 0, bLabelY, 10, rowH, juce::Justification::centredTop, false);
}
