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
    tabBar.setBounds(0, 0, w, tabH);
    const juce::Rectangle<int> content(0, tabH, w, h - tabH);
    for (auto& e : editors) e.setBounds(content);
    matrixPanel.setBounds(content);
}

void ModulatorPanel::paint(juce::Graphics& g)
{
    g.setColour(MuLookAndFeel::colour(MuLookAndFeel::panelBackground));
    g.fillAll();
}
