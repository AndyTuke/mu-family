#include "ModMatrixPanel.h"
#include <thread>

// spin-lock helpers around any ModulationMatrix mutation. Mirrors the pattern
// used by ModulatorEditor::lockMod/unlockMod — without this, addAssignment /
// removeAssignment / setDepth race with the audio thread iterating the
// assignments vector inside ModulationMatrix::process().
namespace
{
    // Returns true if the lock was acquired. Capped at 1000 iterations to avoid
    // stalling the message thread for a full audio block on contention — matches
    // the GatingDesigner::withLock and GatePattern::copyDataFrom patterns.
    // Callers MUST check the return value: only call unlockMods on true return.
    [[nodiscard]] inline bool lockMods(VoiceSlot& s)
    {
        bool expected = false;
        for (int i = 0; i < 1000; ++i)
        {
            if (s.modLock.compare_exchange_strong(expected, true, std::memory_order_acquire))
                return true;
            expected = false;
            std::this_thread::yield();
        }
        jassertfalse; // could not acquire — caller must skip mutation
        return false;
    }
    inline void unlockMods(VoiceSlot& s) noexcept
    {
        s.modLock.store(false, std::memory_order_release);
    }
}

//==============================================================================
ModMatrixPanel::MatrixRow::MatrixRow(const ModulationAssignment& a, int csIndex,
                                      int driveChar, const ModDestProvider* provider)
    : assignId(a.id)
{
    sourceLabel.setText("Mod " + juce::String::charToString('A' + csIndex), juce::dontSendNotification);
    sourceLabel.setColour(juce::Label::textColourId,
                          MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    sourceLabel.setFont(juce::Font(juce::FontOptions{}.withHeight(10.0f)));

    if (provider && provider->populate)
        provider->populate(destCombo, driveChar);
    if (provider && provider->findDropdownId)
    {
        const int ddId = provider->findDropdownId(a.destinationId);
        if (ddId > 0) destCombo.setSelectedId(ddId);
    }

    // shared BipolarSliderRow replaces inline depth + curve juce::Slider setup.
    bipolarPair.setDepth(a.depth, juce::dontSendNotification);
    bipolarPair.setCurve(a.curve, juce::dontSendNotification);
    bipolarPair.onDepthChange = [this](float v) { if (onDepthChange) onDepthChange(v); };
    bipolarPair.onCurveChange = [this](float v) { if (onCurveChange) onCurveChange(v); };

    destCombo.onChange = [this, provider](int id_)
    {
        if (!provider || !provider->resolveId || !onDestChange) return;
        const std::string dest = provider->resolveId(id_);
        if (!dest.empty()) onDestChange(dest);
    };
    removeBtn.onClick = [this] { if (onRemove) onRemove(); };

    addAndMakeVisible(sourceLabel);
    addAndMakeVisible(destCombo);
    addAndMakeVisible(bipolarPair);
    addAndMakeVisible(removeBtn);
}

void ModMatrixPanel::MatrixRow::resized()
{
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int removeW = s(22), srcW = s(46);
    const int pairW = s(BipolarSliderRow::kDepthWidth) + s(2) + s(BipolarSliderRow::kCurveWidth);
    const int destW = w - srcW - pairW - removeW - s(8);
    sourceLabel.setBounds(0,                                          0, srcW,  h);
    destCombo  .setBounds(srcW + s(2),                                   0, destW, h);
    bipolarPair.setBounds(srcW + s(2) + destW + s(2),                       0, pairW, h);
    removeBtn  .setBounds(w - removeW, (h - s(18)) / 2, removeW, s(18));
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
                           MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    addAndMakeVisible(matPageLabel);
    matPrevBtn.onClick = [this] { matPage = juce::jmax(0, matPage - 1); updateMatPager(); resized(); repaint(); };
    matNextBtn.onClick = [this] { matPage++; updateMatPager(); resized(); repaint(); };

    addBtn.onClick = [this]
    {
        if (!voiceSlot) return;
        juce::PopupMenu menu;
        for (int i = 0; i < VoiceSlot::MaxControlSequences; ++i)
            menu.addItem(i + 1, "Mod " + juce::String::charToString('A' + i));
        menu.showMenuAsync(juce::PopupMenu::Options{}, [this](int result)
        {
            if (result < 1 || !voiceSlot) return;
            const int csIdx = result - 1;
            ModulationAssignment a;
            a.id            = "cs" + std::to_string(csIdx) + "_assign_" +
                              juce::Uuid().toString().toStdString();
            a.sourceId      = "cs" + std::to_string(csIdx) + "_output";
            a.destinationId = (destProvider && destProvider->resolveId)
                                  ? destProvider->resolveId(1)
                                  : std::string{};
            a.depth         = 0.0f;
            if (lockMods(*voiceSlot))
            {
                voiceSlot->modulationMatrix.addAssignment(a);
                unlockMods(*voiceSlot);
            }
            rebuildRows();
            resized();
            repaint();
            if (onChange) onChange();
        });
    };
}

void ModMatrixPanel::setVoiceSlot(VoiceSlot* slot)
{
    voiceSlot = slot;
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

void ModMatrixPanel::setDestProvider(const ModDestProvider* p)
{
    destProvider = p;
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
    if (!voiceSlot) return;

    for (const auto& a : voiceSlot->modulationMatrix.getAssignments())
    {
        const int csIdx = csIndexFromSourceId(a.sourceId);
        auto row = std::make_unique<MatrixRow>(a, csIdx, currentDriveChar, destProvider);
        addAndMakeVisible(*row);

        const std::string rowId    = a.id;
        const std::string sourceId = a.sourceId;

        row->onRemove = [this, rowId]
        {
            if (lockMods(*voiceSlot))
            {
                voiceSlot->modulationMatrix.removeAssignment(rowId);
                unlockMods(*voiceSlot);
            }
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
            if (lockMods(*voiceSlot))
            {
                float d = 0.0f;
                float c = 0.0f;
                for (const auto& a2 : voiceSlot->modulationMatrix.getAssignments())
                    if (a2.id == rowId) { d = a2.depth; c = a2.curve; break; }
                voiceSlot->modulationMatrix.removeAssignment(rowId);
                ModulationAssignment na;
                na.id            = rowId;
                na.sourceId      = sourceId;
                na.destinationId = dest;
                na.depth         = d;
                na.curve         = c;
                voiceSlot->modulationMatrix.addAssignment(na);
                unlockMods(*voiceSlot);
            }
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
            if (lockMods(*voiceSlot))
            {
                voiceSlot->modulationMatrix.setDepth(rowId, d);
                unlockMods(*voiceSlot);
            }
            if (onChange) onChange();
        };
        row->onCurveChange = [this, rowId](float c)
        {
            if (lockMods(*voiceSlot))
            {
                voiceSlot->modulationMatrix.setCurve(rowId, c);
                unlockMods(*voiceSlot);
            }
            if (onChange) onChange();
        };

        matrixRows.push_back(std::move(row));
    }
    updateMatPager();
}

void ModMatrixPanel::resized()
{
    using mu_ui::s;
    const int w   = getWidth();
    const int h   = getHeight();
    const int rpp = rowsPerPage();
    const int headerH = s(kHeaderH);
    const int pagerH  = s(kPagerH);
    const int rowH    = s(kRowH);
    const int addBtnH = s(kAddBtnH);

    // Pager row sits just below the header
    const int pagerY = headerH;
    const int btnW   = s(20);
    const int labelW = s(120);
    matPrevBtn .setBounds(w - btnW * 2 - s(4), pagerY, btnW, pagerH);
    matNextBtn .setBounds(w - btnW,            pagerY, btnW, pagerH);
    matPageLabel.setBounds(w - labelW,         pagerY, labelW - btnW * 2 - s(6), pagerH);

    // Rows: show only current page
    const int rowsStart = headerH + pagerH + s(2);
    const int startIdx  = matPage * rpp;
    int y = rowsStart;
    for (int i = 0; i < (int)matrixRows.size(); ++i)
    {
        const bool vis = (i >= startIdx && i < startIdx + rpp);
        matrixRows[i]->setVisible(vis);
        if (vis) { matrixRows[i]->setBounds(0, y, w, rowH); y += rowH + s(2); }
    }

    // Empty-state hint text lands at rowsStart + 8, add button at bottom
    addBtn.setBounds(0, h - addBtnH, w, addBtnH);
}

void ModMatrixPanel::paint(juce::Graphics& g)
{
    using mu_ui::s;
    using mu_ui::sf;
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    g.fillAll();

    const int headerH = s(kHeaderH);
    const int pagerH  = s(kPagerH);

    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::mutedText));
    g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(10.0f))));
    g.drawText("SOURCE",      0,    0, s(46),  headerH, juce::Justification::centredLeft, false);
    g.drawText("DESTINATION", s(48),0, s(200), headerH, juce::Justification::centredLeft, false);
    g.drawText("DEPTH",       getWidth() - s(22) - s(120) - s(4), 0, s(120),
               headerH, juce::Justification::centredLeft, false);

    if (matrixRows.empty())
    {
        g.setFont(juce::Font(juce::FontOptions{}.withHeight(sf(11.0f))));
        // rowsStart = headerH + pagerH + 2; draw hint below that with enough clearance
        g.drawText(juce::String::fromUTF8("No assignments \xe2\x80\x94 use the Mod tabs or Add Assignment below"),
                   0, headerH + pagerH + s(8), getWidth(), s(20),
                   juce::Justification::centred, false);
    }
}
