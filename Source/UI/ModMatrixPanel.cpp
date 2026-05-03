#include "ModMatrixPanel.h"

//==============================================================================
ModMatrixPanel::MatrixRow::MatrixRow(const ModulationAssignment& a, int csIndex)
    : assignId(a.id)
{
    sourceLabel.setText("Mod " + juce::String(char('A' + csIndex)), juce::dontSendNotification);
    sourceLabel.setColour(juce::Label::textColourId,
                          MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    sourceLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));

    for (int i = 0; i < ModDest::ids.size(); ++i)
        destCombo.addItem(ModDest::labels[i], i + 1);
    for (int i = 0; i < ModDest::ids.size(); ++i)
        if (ModDest::ids[i].toStdString() == a.destinationId)
            { destCombo.setSelectedId(i + 1); break; }

    depthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    depthSlider.setRange(-100.0, 100.0, 0.1);
    depthSlider.setValue(a.depth, juce::dontSendNotification);

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

    addAndMakeVisible(sourceLabel);
    addAndMakeVisible(destCombo);
    addAndMakeVisible(depthSlider);
    addAndMakeVisible(removeBtn);
}

void ModMatrixPanel::MatrixRow::resized()
{
    const int w = getWidth(), h = getHeight();
    const int removeW = 22, depthW = 120, srcW = 46;
    const int destW = w - srcW - depthW - removeW - 6;
    sourceLabel.setBounds(0, 0, srcW, h);
    destCombo.setBounds(srcW + 2, 0, destW, h);
    depthSlider.setBounds(srcW + 2 + destW + 2, 0, depthW, h);
    removeBtn.setBounds(w - removeW, (h - 18) / 2, removeW, 18);
}

//==============================================================================
ModMatrixPanel::ModMatrixPanel()
{
    addAndMakeVisible(addBtn);
    addBtn.onClick = [this]
    {
        if (!rhythm) return;
        juce::PopupMenu menu;
        for (int i = 0; i < Rhythm::MaxControlSequences; ++i)
            menu.addItem(i + 1, "Mod " + juce::String(char('A' + i)));
        menu.showMenuAsync(juce::PopupMenu::Options{}, [this](int result)
        {
            if (result < 1 || !rhythm) return;
            const int csIdx = result - 1;
            ModulationAssignment a;
            a.id            = "cs" + std::to_string(csIdx) + "_assign_" +
                              juce::Uuid().toString().toStdString();
            a.sourceId      = "cs" + std::to_string(csIdx) + "_output";
            a.destinationId = ModDest::ids[0].toStdString();
            a.depth         = 0.0f;
            rhythm->modulationMatrix.addAssignment(a);
            rebuildRows();
            resized();
            repaint();
            if (onChange) onChange();
        });
    };
}

void ModMatrixPanel::setRhythm(Rhythm* r)
{
    rhythm = r;
    rebuildRows();
    resized();
    repaint();
}

void ModMatrixPanel::refresh()
{
    rebuildRows();
    resized();
    repaint();
}

static int csIndexFromSourceId(const std::string& sourceId)
{
    if (sourceId.size() >= 3 && sourceId[0] == 'c' && sourceId[1] == 's')
        return juce::jlimit(0, 7, (int)(sourceId[2] - '0'));
    return 0;
}

void ModMatrixPanel::rebuildRows()
{
    for (auto& row : matrixRows) removeChildComponent(row.get());
    matrixRows.clear();
    if (!rhythm) return;

    for (const auto& a : rhythm->modulationMatrix.getAssignments())
    {
        const int csIdx = csIndexFromSourceId(a.sourceId);
        auto row = std::make_unique<MatrixRow>(a, csIdx);
        addAndMakeVisible(*row);

        const std::string rowId    = a.id;
        const std::string sourceId = a.sourceId;

        row->onRemove = [this, rowId]
        {
            rhythm->modulationMatrix.removeAssignment(rowId);
            rebuildRows(); resized(); repaint();
            if (onChange) onChange();
        };
        row->onDestChange = [this, rowId, sourceId](const std::string& dest)
        {
            float d = 0.0f;
            for (const auto& a2 : rhythm->modulationMatrix.getAssignments())
                if (a2.id == rowId) { d = a2.depth; break; }
            rhythm->modulationMatrix.removeAssignment(rowId);
            ModulationAssignment na;
            na.id            = rowId;
            na.sourceId      = sourceId;
            na.destinationId = dest;
            na.depth         = d;
            rhythm->modulationMatrix.addAssignment(na);
            rebuildRows(); resized(); repaint();
            if (onChange) onChange();
        };
        row->onDepthChange = [this, rowId](float d)
        {
            rhythm->modulationMatrix.setDepth(rowId, d);
            if (onChange) onChange();
        };

        matrixRows.push_back(std::move(row));
    }
}

void ModMatrixPanel::resized()
{
    const int w = getWidth();
    int y = kHeaderH;
    for (auto& row : matrixRows)
    {
        row->setBounds(0, y, w, kRowH);
        y += kRowH + 2;
    }
    addBtn.setBounds(0, y + 4, w, kAddBtnH);
}

void ModMatrixPanel::paint(juce::Graphics& g)
{
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillAll();

    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    g.drawText("SOURCE",      0,   0, 46,  kHeaderH, juce::Justification::centredLeft, false);
    g.drawText("DESTINATION", 48,  0, 200, kHeaderH, juce::Justification::centredLeft, false);
    g.drawText("DEPTH",       getWidth() - 22 - 120 - 4, 0, 120,
               kHeaderH, juce::Justification::centredLeft, false);

    if (matrixRows.empty())
    {
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(11.0f)));
        g.drawText("No assignments — use the Mod tabs or Add Assignment below",
                   0, kHeaderH + 8, getWidth(), 20,
                   juce::Justification::centred, false);
    }
}
