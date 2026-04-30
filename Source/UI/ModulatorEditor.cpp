#include "ModulatorEditor.h"

//==============================================================================
ModulatorEditor::AssignmentRow::AssignmentRow(const std::string& assignId)
    : id(assignId)
{
    for (int i = 0; i < ModDest::ids.size(); ++i)
        destCombo.addItem(ModDest::labels[i], i + 1);

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
    addAndMakeVisible(modeCtrl);
    addAndMakeVisible(inputCtrl);
    addAndMakeVisible(lfoEditor);
    addAndMakeVisible(stepEditor);
    addAndMakeVisible(loopNoteSelector);
    addAndMakeVisible(loopMult);
    addAndMakeVisible(stepNoteSelector);
    addAndMakeVisible(stepMult);
    addAndMakeVisible(addBtn);

    wireHeader();
    wireTiming();
}

void ModulatorEditor::setData(ControlSequence* cs_, ModulationMatrix* matrix_,
                               juce::Colour colour, int index)
{
    cs        = cs_;
    matrix    = matrix_;
    modColour = colour;
    modIndex  = index;
    stepEditor.setBarColour(modColour);
    loadFromCS();
    rebuildRows();
    resized();
    repaint();
}

void ModulatorEditor::loadFromCS()
{
    if (!cs) return;

    const bool smooth = (cs->mode == ControlSequence::Mode::Smooth);
    modeCtrl.setSelectedIndex(smooth ? 0 : 1);
    inputCtrl.setSelectedIndex(cs->inputSource == ControlSequence::InputSource::MIDI_CC ? 1 : 0);
    lfoEditor.setVisible(smooth);
    stepEditor.setVisible(!smooth);
    stepNoteSelector.setVisible(!smooth);
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

    loopNoteSelector.setSelection(cs->loopNoteValue, cs->loopNoteMod);
    loopMult.setValue(cs->loopMultiplier);
    stepNoteSelector.setSelection(cs->stepNoteValue, cs->stepNoteMod);
    stepMult.setValue(cs->stepMultiplier);
}

void ModulatorEditor::syncStepValues()
{
    if (!cs) return;
    const int count = cs->getStepCount();
    cs->stepValues.resize(count, 0.0f);
    stepEditor.setSteps(cs->stepValues);
    stepEditor.setStepCount(count);
}

void ModulatorEditor::wireHeader()
{
    modeCtrl.onChange = [this](int idx)
    {
        if (!cs) return;
        cs->mode = (idx == 0) ? ControlSequence::Mode::Smooth : ControlSequence::Mode::Stepped;
        const bool smooth = (cs->mode == ControlSequence::Mode::Smooth);
        lfoEditor.setVisible(smooth);
        stepEditor.setVisible(!smooth);
        stepNoteSelector.setVisible(!smooth);
        stepMult.setVisible(!smooth);
        if (!smooth) syncStepValues();
        else loadFromCS();
        resized();
        if (onChange) onChange();
    };

    inputCtrl.onChange = [this](int idx)
    {
        if (!cs) return;
        cs->inputSource = (idx == 0) ? ControlSequence::InputSource::Internal
                                     : ControlSequence::InputSource::MIDI_CC;
        if (onChange) onChange();
    };
}

void ModulatorEditor::wireTiming()
{
    loopNoteSelector.onChange = [this](NoteValue nv, NoteMod mod)
    {
        if (!cs) return;
        cs->loopNoteValue = nv;
        cs->loopNoteMod   = mod;
        if (cs->mode == ControlSequence::Mode::Stepped) syncStepValues();
        repaint();
        if (onChange) onChange();
    };
    loopMult.onChange = [this](int v)
    {
        if (!cs) return;
        cs->loopMultiplier = v;
        if (cs->mode == ControlSequence::Mode::Stepped) syncStepValues();
        repaint();
        if (onChange) onChange();
    };
    stepNoteSelector.onChange = [this](NoteValue nv, NoteMod mod)
    {
        if (!cs) return;
        cs->stepNoteValue = nv;
        cs->stepNoteMod   = mod;
        syncStepValues();
        repaint();
        if (onChange) onChange();
    };
    stepMult.onChange = [this](int v)
    {
        if (!cs) return;
        cs->stepMultiplier = v;
        syncStepValues();
        repaint();
        if (onChange) onChange();
    };

    lfoEditor.onChange = [this](const std::vector<ControlSequence::CurvePoint>& pts)
    {
        if (!cs) return;
        cs->curvePoints = pts;
        if (onChange) onChange();
    };
    stepEditor.onStepChanged = [this](int idx, float val)
    {
        if (!cs || idx < 0 || idx >= (int)cs->stepValues.size()) return;
        cs->stepValues[idx] = val;
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
    matrix->addAssignment(a);
    rebuildRows();
    resized();
    repaint();
    if (onChange) onChange();
}

void ModulatorEditor::rebuildRows()
{
    for (auto& row : rows) removeChildComponent(row.get());
    rows.clear();
    if (!cs || !matrix) return;

    const juce::String sourceKey = cs->id + "_output";
    for (const auto& a : matrix->getAssignments())
    {
        if (a.sourceId != sourceKey.toStdString()) continue;

        auto row = std::make_unique<AssignmentRow>(a.id);
        addAndMakeVisible(*row);

        for (int i = 0; i < ModDest::ids.size(); ++i)
            if (ModDest::ids[i].toStdString() == a.destinationId)
                { row->destCombo.setSelectedId(i + 1); break; }

        row->depthSlider.setValue(a.depth, juce::dontSendNotification);

        const std::string rowId = a.id;
        row->onRemove = [this, rowId]
        {
            if (matrix) matrix->removeAssignment(rowId);
            rebuildRows();
            resized();
            repaint();
            if (onChange) onChange();
        };
        row->onDestChange = [this, rowId](const std::string& dest)
        {
            if (!matrix) return;
            float d = 0.0f;
            for (const auto& a2 : matrix->getAssignments())
                if (a2.id == rowId) { d = a2.depth; break; }
            matrix->removeAssignment(rowId);
            ModulationAssignment na;
            na.id            = rowId;
            na.sourceId      = cs->id + "_output";
            na.destinationId = dest;
            na.depth         = d;
            matrix->addAssignment(na);
            if (onChange) onChange();
        };
        row->onDepthChange = [this, rowId](float d)
        {
            if (matrix) matrix->setDepth(rowId, d);
            if (onChange) onChange();
        };

        rows.push_back(std::move(row));
    }
}

void ModulatorEditor::resized()
{
    const int w = getWidth();
    int y = 0;

    const int nameW = 76;
    modeCtrl.setBounds(nameW, y, (w - nameW) / 2, kHeaderH);
    inputCtrl.setBounds(nameW + (w - nameW) / 2, y, w - nameW - (w - nameW) / 2, kHeaderH);
    y += kHeaderH;

    lfoEditor.setBounds(0, y, w, kEditorH);
    stepEditor.setBounds(0, y, w, kEditorH);
    y += kEditorH;

    const int nudgeW = 80;
    loopNoteSelector.setBounds(0, y, w - nudgeW, kTimingH);
    loopMult.setBounds(w - nudgeW, y, nudgeW, kTimingH);
    y += kTimingH;

    if (stepNoteSelector.isVisible())
    {
        stepNoteSelector.setBounds(0, y, w - nudgeW, kTimingH);
        stepMult.setBounds(w - nudgeW, y, nudgeW, kTimingH);
        y += kTimingH;
    }

    y += 4;
    for (auto& row : rows)
    {
        row->setBounds(0, y, w, kRowH);
        y += kRowH + 2;
    }

    addBtn.setBounds(0, y, w, kAddBtnH);
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
    g.setFont(juce::Font(11.0f));
    g.drawText("Mod " + juce::String::charToString(char('A' + modIndex)),
               20, 0, 54, kHeaderH, juce::Justification::centredLeft, false);

    if (!cs || cs->mode != ControlSequence::Mode::Stepped) return;

    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    g.setFont(juce::Font(9.0f));
    const int infoY = kHeaderH + kEditorH + kTimingH + kTimingH - 12;
    g.drawText(juce::String(cs->getStepCount()) + " steps",
               getWidth() - 60, infoY, 58, 12,
               juce::Justification::centredRight, false);
}
