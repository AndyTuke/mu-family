#include "ModMatrixPanel.h"
#include <thread>

// #236: spin-lock helpers around any ModulationMatrix mutation. Mirrors the pattern
// used by ModulatorEditor::lockMod/unlockMod — without this, addAssignment /
// removeAssignment / setDepth race with the audio thread iterating the
// assignments vector inside ModulationMatrix::process().
namespace
{
    inline void lockMods(Rhythm& r)
    {
        while (r.modLock.exchange(true, std::memory_order_acquire))
            std::this_thread::yield();
    }
    inline void unlockMods(Rhythm& r) noexcept
    {
        r.modLock.store(false, std::memory_order_release);
    }
}

//==============================================================================
ModMatrixPanel::MatrixRow::MatrixRow(const ModulationAssignment& a, int csIndex, int driveChar)
    : assignId(a.id)
{
    sourceLabel.setText("Mod " + juce::String::charToString('A' + csIndex), juce::dontSendNotification);
    sourceLabel.setColour(juce::Label::textColourId,
                          MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    sourceLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));

    ModDest::populate(destCombo, driveChar);
    for (int i = 0; i < ModDest::ids.size(); ++i)
        if (ModDest::ids[i].toStdString() == a.destinationId)
            { destCombo.setSelectedId(i + 1); break; }

    depthSlider.setSliderStyle(juce::Slider::LinearHorizontal);
    depthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 18);
    depthSlider.setRange(-100.0, 100.0, 0.1);
    depthSlider.setValue(a.depth, juce::dontSendNotification);

    // #224 bipolar curve knob (-100..+100, detent at 0)
    curveSlider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    curveSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 34, 18);
    curveSlider.setRange(-100.0, 100.0, 0.1);
    curveSlider.setValue(a.curve, juce::dontSendNotification);
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

    addAndMakeVisible(sourceLabel);
    addAndMakeVisible(destCombo);
    addAndMakeVisible(depthSlider);
    addAndMakeVisible(curveSlider);
    addAndMakeVisible(removeBtn);
}

void ModMatrixPanel::MatrixRow::resized()
{
    const int w = getWidth(), h = getHeight();
    const int removeW = 22, curveW = 70, depthW = 120, srcW = 46;
    const int destW = w - srcW - depthW - curveW - removeW - 8;
    sourceLabel.setBounds(0,                                          0, srcW,  h);
    destCombo  .setBounds(srcW + 2,                                   0, destW, h);
    depthSlider.setBounds(srcW + 2 + destW + 2,                       0, depthW, h);
    curveSlider.setBounds(srcW + 2 + destW + 2 + depthW + 2,          0, curveW, h);
    removeBtn  .setBounds(w - removeW, (h - 18) / 2, removeW, 18);
}

//==============================================================================
ModMatrixPanel::ModMatrixPanel()
{
    addAndMakeVisible(addBtn);
    addAndMakeVisible(matPrevBtn);
    addAndMakeVisible(matNextBtn);
    matPageLabel.setJustificationType(juce::Justification::centred);
    matPageLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));
    matPageLabel.setColour(juce::Label::textColourId,
                           MuClidLookAndFeel::colour(MuClidLookAndFeel::mutedText));
    addAndMakeVisible(matPageLabel);
    matPrevBtn.onClick = [this] { matPage = juce::jmax(0, matPage - 1); updateMatPager(); resized(); repaint(); };
    matNextBtn.onClick = [this] { matPage++; updateMatPager(); resized(); repaint(); };

    addBtn.onClick = [this]
    {
        if (!rhythm) return;
        juce::PopupMenu menu;
        for (int i = 0; i < Rhythm::MaxControlSequences; ++i)
            menu.addItem(i + 1, "Mod " + juce::String::charToString('A' + i));
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
            lockMods(*rhythm);
            rhythm->modulationMatrix.addAssignment(a);
            unlockMods(*rhythm);
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

void ModMatrixPanel::setInsertAlgorithm(int driveChar)
{
    currentDriveChar = driveChar;
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

int ModMatrixPanel::rowsPerPage() const
{
    const int avail = juce::jmax(0, getHeight() - kHeaderH - kPagerH - kAddBtnH - 8);
    return juce::jmax(1, avail / (kRowH + 2));
}

void ModMatrixPanel::updateMatPager()
{
    const int total = (int)matrixRows.size();
    const int rpp   = rowsPerPage();
    const int pages = juce::jmax(1, (total + rpp - 1) / rpp);
    matPage = juce::jlimit(0, pages - 1, matPage);

    const bool multi = total > rpp;
    matPrevBtn .setVisible(multi);
    matNextBtn .setVisible(multi);
    matPageLabel.setVisible(total > 0);

    if (total == 0) return;

    if (multi)
    {
        matPrevBtn.setEnabled(matPage > 0);
        matNextBtn.setEnabled(matPage < pages - 1);
        matPageLabel.setText(juce::String(matPage + 1) + " / " + juce::String(pages),
                             juce::dontSendNotification);
    }
    else
    {
        matPageLabel.setText(juce::String(total) + (total == 1 ? " assignment" : " assignments"),
                             juce::dontSendNotification);
    }
}

void ModMatrixPanel::rebuildRows()
{
    for (auto& row : matrixRows) removeChildComponent(row.get());
    matrixRows.clear();
    matPage = 0;
    if (!rhythm) return;

    for (const auto& a : rhythm->modulationMatrix.getAssignments())
    {
        const int csIdx = csIndexFromSourceId(a.sourceId);
        auto row = std::make_unique<MatrixRow>(a, csIdx, currentDriveChar);
        addAndMakeVisible(*row);

        const std::string rowId    = a.id;
        const std::string sourceId = a.sourceId;

        row->onRemove = [this, rowId]
        {
            lockMods(*rhythm);
            rhythm->modulationMatrix.removeAssignment(rowId);
            unlockMods(*rhythm);
            if (onChange) onChange();
            juce::Component::SafePointer<ModMatrixPanel> safe(this);
            juce::MessageManager::callAsync([safe]
            {
                if (auto* p = safe.getComponent())
                    { p->rebuildRows(); p->resized(); p->repaint(); }
            });
        };
        row->onDestChange = [this, rowId, sourceId](const std::string& dest)
        {
            float d = 0.0f;
            float c = 0.0f;
            lockMods(*rhythm);
            for (const auto& a2 : rhythm->modulationMatrix.getAssignments())
                if (a2.id == rowId) { d = a2.depth; c = a2.curve; break; }
            rhythm->modulationMatrix.removeAssignment(rowId);
            ModulationAssignment na;
            na.id            = rowId;
            na.sourceId      = sourceId;
            na.destinationId = dest;
            na.depth         = d;
            na.curve         = c;   // #224
            rhythm->modulationMatrix.addAssignment(na);
            unlockMods(*rhythm);
            if (onChange) onChange();
            juce::Component::SafePointer<ModMatrixPanel> safe(this);
            juce::MessageManager::callAsync([safe]
            {
                if (auto* p = safe.getComponent())
                    { p->rebuildRows(); p->resized(); p->repaint(); }
            });
        };
        row->onDepthChange = [this, rowId](float d)
        {
            lockMods(*rhythm);
            rhythm->modulationMatrix.setDepth(rowId, d);
            unlockMods(*rhythm);
            if (onChange) onChange();
        };
        row->onCurveChange = [this, rowId](float c)   // #224
        {
            lockMods(*rhythm);
            rhythm->modulationMatrix.setCurve(rowId, c);
            unlockMods(*rhythm);
            if (onChange) onChange();
        };

        matrixRows.push_back(std::move(row));
    }
    updateMatPager();
}

void ModMatrixPanel::resized()
{
    const int w   = getWidth();
    const int h   = getHeight();
    const int rpp = rowsPerPage();

    // Pager row sits just below the header
    const int pagerY = kHeaderH;
    const int btnW   = 20;
    matPrevBtn .setBounds(w - btnW * 2 - 4, pagerY, btnW, kPagerH);
    matNextBtn .setBounds(w - btnW,          pagerY, btnW, kPagerH);
    matPageLabel.setBounds(w - 120,          pagerY, 120 - btnW * 2 - 6, kPagerH);

    // Rows: show only current page
    const int rowsStart = kHeaderH + kPagerH + 2;
    const int startIdx  = matPage * rpp;
    int y = rowsStart;
    for (int i = 0; i < (int)matrixRows.size(); ++i)
    {
        const bool vis = (i >= startIdx && i < startIdx + rpp);
        matrixRows[i]->setVisible(vis);
        if (vis) { matrixRows[i]->setBounds(0, y, w, kRowH); y += kRowH + 2; }
    }

    // Empty-state hint text lands at rowsStart + 8, add button at bottom
    addBtn.setBounds(0, h - kAddBtnH, w, kAddBtnH);
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
        // rowsStart = kHeaderH + kPagerH + 2; draw hint below that with enough clearance
        g.drawText(juce::String::fromUTF8("No assignments \xe2\x80\x94 use the Mod tabs or Add Assignment below"),
                   0, kHeaderH + kPagerH + 8, getWidth(), 20,
                   juce::Justification::centred, false);
    }
}
