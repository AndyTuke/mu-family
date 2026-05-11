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

    depthSlider.onValueChange = [this]
    {
        if (onDepthChange) onDepthChange((float)depthSlider.getValue());
    };
    destCombo.onChange = [this](int id_)
    {
        if (id_ >= 1 && id_ <= ModDest::ids.size() && onDestChange)
            onDestChange(ModDest::ids[id_ - 1].toStdString());
    };
    removeBtn.onClick = [this] { if (onRemove) onRemove(); };

    addAndMakeVisible(destCombo);
    addAndMakeVisible(depthSlider);
    addAndMakeVisible(removeBtn);
}

void ModulatorEditor::AssignmentRow::resized()
{
    const int w = getWidth(), h = getHeight();
    const int removeW = 22, depthW = 130;
    const int destW = w - depthW - removeW - 4;
    destCombo.setBounds(0, 0, destW, h);
    depthSlider.setBounds(destW + 2, 0, depthW, h);
    removeBtn.setBounds(w - removeW, (h - 18) / 2, removeW, 18);
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
    rowsViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(rowsViewport);
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
    loadFromCS();
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

void ModulatorEditor::rebuildRows()
{
    for (auto& row : rows) rowsBox.removeChildComponent(row.get());
    rows.clear();
    if (!cs || !matrix) return;

    const juce::String sourceKey = cs->id + "_output";
    for (const auto& a : matrix->getAssignments())
    {
        if (a.sourceId != sourceKey.toStdString()) continue;

        auto row = std::make_unique<AssignmentRow>(a.id, currentDriveChar);
        rowsBox.addAndMakeVisible(*row);

        for (int i = 0; i < ModDest::ids.size(); ++i)
            if (ModDest::ids[i].toStdString() == a.destinationId)
                { row->destCombo.setSelectedId(i + 1); break; }

        row->depthSlider.setValue(a.depth, juce::dontSendNotification);

        const std::string rowId = a.id;
        row->onRemove = [this, rowId]
        {
            if (!matrix) return;
            lockMod();
            matrix->removeAssignment(rowId);
            unlockMod();
            rebuildRows();
            resized();
            repaint();
            if (onChange) onChange();
        };
        row->onDestChange = [this, rowId](const std::string& dest)
        {
            if (!matrix) return;
            float d = 0.0f;
            lockMod();
            for (const auto& a2 : matrix->getAssignments())
                if (a2.id == rowId) { d = a2.depth; break; }
            matrix->removeAssignment(rowId);
            ModulationAssignment na;
            na.id            = rowId;
            na.sourceId      = cs->id + "_output";
            na.destinationId = dest;
            na.depth         = d;
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

        rows.push_back(std::move(row));
    }
}

void ModulatorEditor::resized()
{
    const int w = getWidth();
    int y = 0;

    // Header: [dot+name painted] | [mode dropdown] | [Uni/Bi polarity]
    const int nameW  = 76;
    const int modeW  = 90;
    const int polW   = 52;
    modeDropdown.setBounds(nameW, y, modeW, kHeaderH);
    polarityCtrl.setBounds(nameW + modeW + 4, y, polW, kHeaderH);
    y += kHeaderH;

    // LFO / Step editor
    lfoEditor.setBounds(0, y, w, kEditorH);
    stepEditor.setBounds(0, y, w, kEditorH);
    y += kEditorH;

    // Timing rows — both start at x=0, no polarity offset.
    const int labelW = 36;   // "Loop" / "Step"
    const int dropW  = 65;   // fits longest note-value label "1/32T"
    const int nudgeW = 52;   // "× 4" inline label + value + arrows

    loopLabel.setBounds(0, y, labelW, kTimingH);
    loopDropdown.setBounds(labelW + 2, y, dropW, kTimingH);
    loopMult.setBounds(labelW + 2 + dropW + 4, y, nudgeW, kTimingH);
    y += kTimingH;

    if (stepDropdown.isVisible())
    {
        stepLabel.setBounds(0, y, labelW, kTimingH);
        stepDropdown.setBounds(labelW + 2, y, dropW, kTimingH);
        stepMult.setBounds(labelW + 2 + dropW + 4, y, nudgeW, kTimingH);
        y += kTimingH;
    }

    y += 4;
    const int viewportH = juce::jmax(0, getHeight() - y - kAddBtnH - 2);
    rowsViewport.setBounds(0, y, w, viewportH);
    const int contentH = juce::jmax(viewportH, (int)rows.size() * (kRowH + 2));
    rowsBox.setSize(w, contentH);
    int ry = 0;
    for (auto& row : rows)
    {
        row->setBounds(0, ry, w, kRowH);
        ry += kRowH + 2;
    }

    addBtn.setBounds(0, getHeight() - kAddBtnH, w, kAddBtnH);
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

    if (!cs || cs->mode != ControlSequence::Mode::Stepped) return;

    // Step count info drawn at right of the second timing row (step row, below loop row)
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(9.0f)));
    const int infoY = kHeaderH + kEditorH + kTimingH * 2 - 12;
    g.drawText(juce::String(cs->getStepCount()) + " steps",
               getWidth() - 60, infoY, 58, 12,
               juce::Justification::centredRight, false);
}
