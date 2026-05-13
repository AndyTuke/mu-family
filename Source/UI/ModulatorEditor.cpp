#include "ModulatorEditor.h"

//==============================================================================
// Note value lookup table for timing dropdowns (id = index + 1)
namespace {
struct NoteEntry { NoteValue nv; NoteMod mod; const char* label; };
static constexpr NoteEntry kNoteEntries[] = {
    { NoteValue::Whole,        NoteMod::None,    "1"     },
    { NoteValue::Half,         NoteMod::None,    "1/2"   },
    { NoteValue::Quarter,      NoteMod::None,    "1/4"   },
    { NoteValue::Eighth,       NoteMod::None,    "1/8"   },
    { NoteValue::Sixteenth,    NoteMod::None,    "1/16"  },
    { NoteValue::ThirtySecond, NoteMod::None,    "1/32"  },
    { NoteValue::Whole,        NoteMod::Triplet, "1T"    },
    { NoteValue::Half,         NoteMod::Triplet, "1/2T"  },
    { NoteValue::Quarter,      NoteMod::Triplet, "1/4T"  },
    { NoteValue::Eighth,       NoteMod::Triplet, "1/8T"  },
    { NoteValue::Sixteenth,    NoteMod::Triplet, "1/16T" },
    { NoteValue::ThirtySecond, NoteMod::Triplet, "1/32T" },
    { NoteValue::Whole,        NoteMod::Dotted,  "1."    },
    { NoteValue::Half,         NoteMod::Dotted,  "1/2."  },
    { NoteValue::Quarter,      NoteMod::Dotted,  "1/4."  },
    { NoteValue::Eighth,       NoteMod::Dotted,  "1/8."  },
    { NoteValue::Sixteenth,    NoteMod::Dotted,  "1/16." },
    { NoteValue::ThirtySecond, NoteMod::Dotted,  "1/32." },
};
static constexpr int kNoteEntryCount = (int)(sizeof(kNoteEntries) / sizeof(kNoteEntries[0]));

static int noteToId(NoteValue nv, NoteMod mod)
{
    for (int i = 0; i < kNoteEntryCount; ++i)
        if (kNoteEntries[i].nv == nv && kNoteEntries[i].mod == mod)
            return i + 1;
    return 3; // fallback: 1/4
}

static void idToNote(int id, NoteValue& nv, NoteMod& mod)
{
    int i = id - 1;
    if (i >= 0 && i < kNoteEntryCount) { nv = kNoteEntries[i].nv; mod = kNoteEntries[i].mod; }
    else                               { nv = NoteValue::Quarter;  mod = NoteMod::None;       }
}

static void populateNoteDropdown(DropdownSelect& dd)
{
    for (int i = 0; i < kNoteEntryCount; ++i)
        dd.addItem(kNoteEntries[i].label, i + 1);
}
} // namespace

//==============================================================================
ModulatorEditor::AssignmentRow::AssignmentRow(const std::string& assignId, int driveChar)
    : id(assignId)
{
    ModDest::populate(destCombo, driveChar);

    depthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    depthSlider.setRange(-100.0, 100.0, 0.1);
    depthSlider.setValue(0.0, juce::dontSendNotification);

    // #224 curve knob: rotary, bipolar with detent at 0.
    curveSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    curveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
    curveSlider.setRange(-100.0, 100.0, 0.1);
    curveSlider.setValue(0.0, juce::dontSendNotification);
    curveSlider.setDoubleClickReturnValue(true, 0.0);
    curveSlider.setTooltip("Curve: log .. linear .. exp (#224)");

    depthSlider.onValueChange = [this]
    {
        if (onDepthChange) onDepthChange((float)depthSlider.getValue());
    };
    curveSlider.onValueChange = [this]
    {
        if (onCurveChange) onCurveChange((float)curveSlider.getValue());
    };
    destCombo.onChange = [this](int id_)
    {
        if (id_ >= 1 && id_ <= ModDest::ids.size() && onDestChange)
            onDestChange(ModDest::ids[id_ - 1].toStdString());
    };
    removeBtn.onClick = [this] { if (onRemove) onRemove(); };

    addAndMakeVisible(destCombo);
    addAndMakeVisible(depthSlider);
    addAndMakeVisible(curveSlider);
    addAndMakeVisible(removeBtn);
}

void ModulatorEditor::AssignmentRow::resized()
{
    const int w = getWidth(), h = getHeight();
    const int numW = 20, removeW = 22, curveW = 70, depthW = 130;
    const int destW = w - numW - depthW - curveW - removeW - 8;
    destCombo  .setBounds(numW + 2,                              0, destW,  h);
    depthSlider.setBounds(numW + 2 + destW + 2,                  0, depthW, h);
    curveSlider.setBounds(numW + 2 + destW + 2 + depthW + 2,     0, curveW, h);
    removeBtn  .setBounds(w - removeW, (h - 18) / 2, removeW, 18);
}

void ModulatorEditor::AssignmentRow::paint(juce::Graphics& g)
{
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    g.drawText(juce::String(rowNumber) + ".",
               2, 0, 18, getHeight(), juce::Justification::centred, false);
}

//==============================================================================
ModulatorEditor::ModulatorEditor()
{
    // Mode dropdown replaces SegmentControl (#157)
    modeDropdown.addItem("Smooth",  1);
    modeDropdown.addItem("Stepped", 2);
    addAndMakeVisible(modeDropdown);
    addAndMakeVisible(polarityCtrl);  // placed below editor (#156)
    addAndMakeVisible(lfoEditor);
    addAndMakeVisible(stepEditor);

    // Loop timing row
    loopLabel.setText("Loop", juce::dontSendNotification);
    loopLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    loopLabel.setJustificationType(juce::Justification::centredRight);
    loopLabel.setColour(juce::Label::textColourId,
                        MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    populateNoteDropdown(loopDropdown);
    loopMult.setShowStepButtons(false);
    loopMult.setLabelInline(true);
    addAndMakeVisible(loopLabel);
    addAndMakeVisible(loopDropdown);
    addAndMakeVisible(loopMult);

    // Step timing row — Stepped mode only
    stepLabel.setText("Step", juce::dontSendNotification);
    stepLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    stepLabel.setJustificationType(juce::Justification::centredRight);
    stepLabel.setColour(juce::Label::textColourId,
                        MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    populateNoteDropdown(stepDropdown);
    stepMult.setShowStepButtons(false);
    stepMult.setLabelInline(true);
    addAndMakeVisible(stepLabel);
    addAndMakeVisible(stepDropdown);
    addAndMakeVisible(stepMult);
    stepLabel.setVisible(false);
    stepDropdown.setVisible(false);
    stepMult.setVisible(false);

    rowsViewport.setViewedComponent(&rowsBox, false);
    rowsViewport.setScrollBarsShown(false, false);
    addAndMakeVisible(rowsViewport);

    rowPageLabel.setJustificationType(juce::Justification::centred);
    rowPageLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    rowPageLabel.setColour(juce::Label::textColourId,
                           MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    addAndMakeVisible(rowPrevBtn);
    addAndMakeVisible(rowNextBtn);
    addAndMakeVisible(rowPageLabel);
    rowPrevBtn.onClick = [this] { scrollRowPage(-1); };
    rowNextBtn.onClick = [this] { scrollRowPage(+1); };

    addAndMakeVisible(addBtn);

    wireHeader();
    wireTiming();
}

void ModulatorEditor::setPlayheadBeat(double beat)
{
    if (!cs) return;
    const double loopBeatsRaw = cs->getLoopLengthBeats();
    const double loopBeats    = loopBeatsRaw > 0.0 ? loopBeatsRaw : 1.0;
    const float  phase        = static_cast<float>(std::fmod(beat / loopBeats, 1.0));
    if (cs->mode == ControlSequence::Mode::Smooth)
        lfoEditor.setPlayheadPhase(phase);
    else
        stepEditor.setPlayheadPhase(phase);
}

void ModulatorEditor::setData(ControlSequence* cs_, ModulationMatrix* matrix_,
                               juce::Colour colour, int index,
                               std::atomic<bool>* lock)
{
    cs             = cs_;
    matrix         = matrix_;
    rhythmModLock  = lock;
    modColour      = colour;
    modIndex       = index;
    stepEditor.setBarColour(modColour);
    // #229: loadFromCS() can `push_back` to cs->curvePoints (seeding default points)
    // and `resize` cs->stepValues — both can race with the audio thread's
    // `cs->evaluate()` if we don't hold modLock during the mutation.
    lockMod();
    loadFromCS();
    unlockMod();
    rebuildRows();
    resized();
    repaint();
}

void ModulatorEditor::setInsertAlgorithm(int driveChar)
{
    currentDriveChar = driveChar;
    rebuildRows();
    resized();
    repaint();
}

void ModulatorEditor::lockMod()
{
    if (!rhythmModLock) return;
    bool expected = false;
    while (!rhythmModLock->compare_exchange_weak(expected, true, std::memory_order_acquire))
        expected = false;
}

void ModulatorEditor::unlockMod()
{
    if (rhythmModLock)
        rhythmModLock->store(false, std::memory_order_release);
}

void ModulatorEditor::loadFromCS()
{
    if (!cs) return;

    const bool smooth   = (cs->mode == ControlSequence::Mode::Smooth);
    const bool unipolar = (cs->polarity == ControlSequence::Polarity::Unipolar);
    modeDropdown.setSelectedId(smooth ? 1 : 2);
    polarityCtrl.setSelectedIndex(unipolar ? 0 : 1);
    lfoEditor.setUnipolar(unipolar);
    stepEditor.setUnipolar(unipolar);
    lfoEditor.setVisible(smooth);
    stepEditor.setVisible(!smooth);
    stepLabel.setVisible(!smooth);
    stepDropdown.setVisible(!smooth);
    stepMult.setVisible(!smooth);

    if (smooth)
    {
        if (cs->curvePoints.empty())
        {
            cs->curvePoints.push_back({ 0.0f, 0.0f, false, 0.0f, 0.0f });
            cs->curvePoints.push_back({ 1.0f, 0.0f, false, 0.0f, 0.0f });
        }
        lfoEditor.setPoints(cs->curvePoints);
    }
    else
    {
        syncStepValues();
    }

    loopDropdown.setSelectedId(noteToId(cs->loopNoteValue, cs->loopNoteMod));
    loopMult.setValue(cs->loopMultiplier);
    stepDropdown.setSelectedId(noteToId(cs->stepNoteValue, cs->stepNoteMod));
    stepMult.setValue(cs->stepMultiplier);
}

void ModulatorEditor::syncStepValues()
{
    if (!cs) return;
    const int rawCount = cs->getStepCount();
    // Sanity-bound the count: anything beyond a few thousand indicates a corrupt
    // ControlSequence pointer (e.g. dangling after a Rhythm vector erase).
    if (rawCount < 1 || rawCount > 4096)
    {
        DBG("ModulatorEditor::syncStepValues: garbage step count " << rawCount
            << " -- cs likely dangles. Skipping.");
        jassertfalse;
        return;
    }
    cs->stepValues.resize((size_t)rawCount, 0.0f);
    stepEditor.setSteps(cs->stepValues);
    stepEditor.setStepCount(rawCount);
}

void ModulatorEditor::wireHeader()
{
    modeDropdown.onChange = [this](int id)
    {
        if (!cs) return;
        lockMod();
        cs->mode = (id == 1) ? ControlSequence::Mode::Smooth : ControlSequence::Mode::Stepped;
        unlockMod();
        const bool smooth = (cs->mode == ControlSequence::Mode::Smooth);
        lfoEditor.setVisible(smooth);
        stepEditor.setVisible(!smooth);
        stepLabel.setVisible(!smooth);
        stepDropdown.setVisible(!smooth);
        stepMult.setVisible(!smooth);
        if (!smooth) syncStepValues();
        else loadFromCS();
        resized();
        if (onChange) onChange();
    };

    polarityCtrl.onChange = [this](int idx)
    {
        if (!cs) return;
        const bool unipolar = (idx == 0);
        lockMod();
        cs->polarity = unipolar ? ControlSequence::Polarity::Unipolar
                                : ControlSequence::Polarity::Bipolar;
        unlockMod();
        lfoEditor.setUnipolar(unipolar);
        stepEditor.setUnipolar(unipolar);
        if (onChange) onChange();
    };
}

void ModulatorEditor::wireTiming()
{
    loopDropdown.onChange = [this](int id)
    {
        if (!cs) return;
        NoteValue nv; NoteMod mod;
        idToNote(id, nv, mod);
        lockMod();
        cs->loopNoteValue = nv;
        cs->loopNoteMod   = mod;
        if (cs->mode == ControlSequence::Mode::Stepped) syncStepValues();
        unlockMod();
        repaint();
        if (onChange) onChange();
    };
    loopMult.onChange = [this](int v)
    {
        if (!cs) return;
        lockMod();
        cs->loopMultiplier = v;
        if (cs->mode == ControlSequence::Mode::Stepped) syncStepValues();
        unlockMod();
        repaint();
        if (onChange) onChange();
    };
    stepDropdown.onChange = [this](int id)
    {
        if (!cs) return;
        NoteValue nv; NoteMod mod;
        idToNote(id, nv, mod);
        lockMod();
        cs->stepNoteValue = nv;
        cs->stepNoteMod   = mod;
        syncStepValues();
        unlockMod();
        repaint();
        if (onChange) onChange();
    };
    stepMult.onChange = [this](int v)
    {
        if (!cs) return;
        lockMod();
        cs->stepMultiplier = v;
        syncStepValues();
        unlockMod();
        repaint();
        if (onChange) onChange();
    };

    lfoEditor.onChange = [this](const std::vector<ControlSequence::CurvePoint>& pts)
    {
        if (!cs) return;
        lockMod();
        cs->curvePoints = pts;
        unlockMod();
        if (onChange) onChange();
    };
    stepEditor.onStepChanged = [this](int idx, float val)
    {
        if (!cs || idx < 0 || idx >= (int)cs->stepValues.size()) return;
        lockMod();
        cs->stepValues[idx] = val;
        unlockMod();
        if (onChange) onChange();
    };

    addBtn.onClick = [this] { addTarget(); };
}

void ModulatorEditor::addTarget()
{
    if (!cs || !matrix) return;
    ModulationAssignment a;
    a.id            = cs->id + "_assign_" + juce::Uuid().toString().toStdString();
    a.sourceId      = cs->id + "_output";
    a.destinationId = ModDest::ids[0].toStdString();
    a.depth         = 0.0f;
    lockMod();
    matrix->addAssignment(a);
    unlockMod();
    rebuildRows();
    resized();
    repaint();
    if (onChange) onChange();
}

void ModulatorEditor::updateRowPager()
{
    const int total = (int)rows.size();
    const int viewH = rowsViewport.getHeight();
    const int rpp   = juce::jmax(1, viewH / (kRowH + 2));
    const bool multi = total > rpp;

    rowPrevBtn  .setVisible(multi);
    rowNextBtn  .setVisible(multi);
    rowPageLabel.setVisible(total > 0);

    if (total == 0) return;

    if (multi)
    {
        const int curTop = viewH > 0 ? (rowsViewport.getViewPositionY() / (kRowH + 2)) : 0;
        const int shown  = juce::jmin(rpp, total - curTop);
        rowPrevBtn.setEnabled(curTop > 0);
        rowNextBtn.setEnabled(curTop + rpp < total);
        rowPageLabel.setText(juce::String(curTop + 1) + " \xe2\x80\x93 " +
                             juce::String(curTop + shown) + " / " + juce::String(total),
                             juce::dontSendNotification);
    }
    else
    {
        rowPageLabel.setText(juce::String(total) + (total == 1 ? " target" : " targets"),
                             juce::dontSendNotification);
    }
}

void ModulatorEditor::scrollRowPage(int delta)
{
    const int curY = rowsViewport.getViewPositionY();
    const int newY = juce::jmax(0, curY + delta * (kRowH + 2));
    rowsViewport.setViewPosition(0, newY);
    updateRowPager();
    repaint();
}

void ModulatorEditor::rebuildRows()
{
    for (auto& row : rows) rowsBox.removeChildComponent(row.get());
    rows.clear();
    rowsViewport.setViewPosition(0, 0);
    if (!cs || !matrix) return;

    const juce::String sourceKey = cs->id + "_output";
    for (const auto& a : matrix->getAssignments())
    {
        if (a.sourceId != sourceKey.toStdString()) continue;

        auto row = std::make_unique<AssignmentRow>(a.id, currentDriveChar);
        row->rowNumber = (int)rows.size() + 1;
        rowsBox.addAndMakeVisible(*row);

        for (int i = 0; i < ModDest::ids.size(); ++i)
            if (ModDest::ids[i].toStdString() == a.destinationId)
                { row->destCombo.setSelectedId(i + 1); break; }

        row->depthSlider.setValue(a.depth, juce::dontSendNotification);
        row->curveSlider.setValue(a.curve, juce::dontSendNotification);   // #224

        const std::string rowId = a.id;
        row->onRemove = [this, rowId]
        {
            if (!matrix) return;
            lockMod();
            matrix->removeAssignment(rowId);
            unlockMod();
            if (onChange) onChange();
            juce::Component::SafePointer<ModulatorEditor> safe(this);
            juce::MessageManager::callAsync([safe]
            {
                if (auto* p = safe.getComponent())
                    { p->rebuildRows(); p->resized(); p->repaint(); }
            });
        };
        row->onDestChange = [this, rowId](const std::string& dest)
        {
            if (!matrix) return;
            float d = 0.0f;
            float c = 0.0f;
            lockMod();
            for (const auto& a2 : matrix->getAssignments())
                if (a2.id == rowId) { d = a2.depth; c = a2.curve; break; }
            matrix->removeAssignment(rowId);
            ModulationAssignment na;
            na.id            = rowId;
            na.sourceId      = cs->id + "_output";
            na.destinationId = dest;
            na.depth         = d;
            na.curve         = c;   // #224: preserve curve through dest change
            matrix->addAssignment(na);
            unlockMod();
            if (onChange) onChange();
        };
        row->onDepthChange = [this, rowId](float d)
        {
            if (!matrix) return;
            lockMod();
            matrix->setDepth(rowId, d);
            unlockMod();
            if (onChange) onChange();
        };
        row->onCurveChange = [this, rowId](float c)   // #224
        {
            if (!matrix) return;
            lockMod();
            matrix->setCurve(rowId, c);
            unlockMod();
            if (onChange) onChange();
        };

        rows.push_back(std::move(row));
    }
}

void ModulatorEditor::resized()
{
    const int w = getWidth(), h = getHeight();

    // ── Single header row: [● Mod X painted] [mode] [polarity] [Loop dd mult] [Step dd mult] ──
    const int nameW  = 68;
    const int modeW  = 78;
    const int polW   = 44;
    const int lbW    = 30;   // "Loop" / "Step" label
    const int ddW    = 58;   // note-value dropdown (fits "1/32T")
    const int nmW    = 46;   // nudge "× N"

    int x = nameW;
    modeDropdown.setBounds(x, 0, modeW, kHeaderH); x += modeW + 4;
    polarityCtrl.setBounds(x, 0, polW,  kHeaderH); x += polW + 8;
    loopLabel   .setBounds(x, 0, lbW,   kHeaderH); x += lbW + 2;
    loopDropdown.setBounds(x, 0, ddW,   kHeaderH); x += ddW + 2;
    loopMult    .setBounds(x, 0, nmW,   kHeaderH); x += nmW + 8;

    if (stepDropdown.isVisible())
    {
        stepLabel   .setBounds(x, 0, lbW, kHeaderH); x += lbW + 2;
        stepDropdown.setBounds(x, 0, ddW, kHeaderH); x += ddW + 2;
        stepMult    .setBounds(x, 0, nmW, kHeaderH);
    }

    // ── LFO / Step editor ──────────────────────────────────────────────────────
    lfoEditor .setBounds(0, kHeaderH, w, kEditorH);
    stepEditor.setBounds(0, kHeaderH, w, kEditorH);

    // ── Assignment rows viewport ───────────────────────────────────────────────
    const int editorBottom = kHeaderH + kEditorH + 4;
    const int viewH = juce::jmax(0, h - editorBottom - kPagerH - kAddBtnH - 4);
    rowsViewport.setBounds(0, editorBottom, w, viewH);

    const int contentH = juce::jmax(viewH, (int)rows.size() * (kRowH + 2));
    rowsBox.setSize(w, contentH);
    int ry = 0;
    for (auto& row : rows) { row->setBounds(0, ry, w, kRowH); ry += kRowH + 2; }

    // ── Pager row ─────────────────────────────────────────────────────────────
    const int pagerY = editorBottom + viewH + 2;
    const int btnW   = 20;
    rowPrevBtn  .setBounds(0,          pagerY, btnW, kPagerH);
    rowPageLabel.setBounds(btnW + 2,   pagerY, w - btnW * 2 - 4, kPagerH);
    rowNextBtn  .setBounds(w - btnW,   pagerY, btnW, kPagerH);

    // ── Add button ─────────────────────────────────────────────────────────────
    addBtn.setBounds(0, h - kAddBtnH, w, kAddBtnH);

    updateRowPager();
}

void ModulatorEditor::paint(juce::Graphics& g)
{
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillAll();

    // Header: colour dot + modulator name
    const float dotY = (kHeaderH - 8) * 0.5f;
    g.setColour(modColour);
    g.fillEllipse(8.0f, dotY, 8.0f, 8.0f);
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::headingText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
    g.drawText("Mod " + juce::String::charToString(char('A' + modIndex)),
               20, 0, 54, kHeaderH, juce::Justification::centredLeft, false);

    // Step count drawn at far right of header row (Stepped mode only)
    if (cs && cs->mode == ControlSequence::Mode::Stepped)
    {
        g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
        g.drawText(juce::String(cs->getStepCount()) + " steps",
                   getWidth() - 58, 0, 56, kHeaderH,
                   juce::Justification::centredRight, false);
    }
}
