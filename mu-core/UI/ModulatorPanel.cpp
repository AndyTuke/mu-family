#include "ModulatorPanel.h"

juce::Colour ModulatorPanel::modColour(int index) noexcept
{
    using Id = MuLookAndFeel::ColourIds;
    // All 8 label colours live in MuLookAndFeel so the design system is the
    // single source of truth. modLabelA..H are consecutive IDs, so index directly.
    return MuLookAndFeel::colour(static_cast<Id>(Id::modLabelA + juce::jlimit(0, 7, index)));
}

ModulatorPanel::ModulatorPanel()
{
    addAndMakeVisible(tabBar);
    for (auto& e : editors) addAndMakeVisible(e);
    addAndMakeVisible(matrixPanel);

    showTab(0);

    tabBar.onChange = [this](int idx)
    {
        activeTab = idx;
        showTab(idx);
    };
}

void ModulatorPanel::setMetalStyle(bool m)
{
    metal = m;
    if (m) setOpaqueBackground(false);
    tabBar.setDrawStyle(m ? SegmentControl::DrawStyle::Lcd : SegmentControl::DrawStyle::Bar);
    for (auto& e : editors) e.setMetalStyle(m);
    matrixPanel.setMetalStyle(m);
    resized();
}

void ModulatorPanel::showTab(int idx)
{
    for (int i = 0; i < kNumMods; ++i)
        editors[i].setVisible(idx == i);
    matrixPanel.setVisible(idx == kNumMods);
    if (idx == kNumMods)
        matrixPanel.refresh();
}

void ModulatorPanel::setInsertAlgorithm(int driveChar)
{
    for (auto& e : editors)
        e.setInsertAlgorithm(driveChar);
    matrixPanel.setInsertAlgorithm(driveChar);
}

void ModulatorPanel::setDestProvider(const ModDestProvider* p)
{
    destProvider = p;
    for (auto& e : editors)
        e.setDestProvider(p);
    matrixPanel.setDestProvider(p);
}

void ModulatorPanel::setPlayheadBeat(double beat)
{
    if (activeTab < kNumMods)
        editors[activeTab].setPlayheadBeat(beat);
}

void ModulatorPanel::setVoiceSlot(VoiceSlot* slot)
{
    voiceSlot = slot;
    if (!slot)
    {
        // Clear stale pointers in the editors and matrix panel — otherwise their
        // ControlSequence*/ModulationMatrix* still point inside a destroyed VoiceSlot.
        for (int i = 0; i < kNumMods; ++i)
            editors[i].setData(nullptr, nullptr, modColour(i), i, nullptr);
        matrixPanel.setVoiceSlot(nullptr);
        return;
    }

    for (int i = 0; i < kNumMods; ++i)
    {
        editors[i].setData(&slot->controlSequences[i], &slot->modulationMatrix,
                            modColour(i), i, &slot->modLock.v);
        editors[i].onChange = [this] { if (onChange) onChange(); };
    }
    matrixPanel.setVoiceSlot(slot);
    matrixPanel.onChange = [this] { if (onChange) onChange(); };
}

void ModulatorPanel::resized()
{
    using mu_ui::s;
    const int w = getWidth(), h = getHeight();
    const int tabH = s(kTabH);
    // With screws the tabs are a little smaller — in from the sides and a touch shorter —
    // so they don't crowd the panel's corner screws.
    if (MuLookAndFeel::hasScrews(*this))
        tabBar.setBounds(juce::Rectangle<int>(0, 0, w, tabH).reduced(s(MuLookAndFeel::kSpaceS), s(2)));
    else
        tabBar.setBounds(0, 0, w, tabH);
    const int gap = metal ? s(MuLookAndFeel::kSpaceXS) : 0;   // metal: the boxes' shadows need room
    const juce::Rectangle<int> content(0, tabH + gap, w, h - tabH - gap);
    for (auto& e : editors) e.setBounds(content);
    matrixPanel.setBounds(content);
}

void ModulatorPanel::paint(juce::Graphics& g)
{
    if (! fillBackground) return;
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    g.fillAll();
}
