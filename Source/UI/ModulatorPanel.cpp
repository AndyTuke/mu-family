#include "ModulatorPanel.h"

juce::Colour ModulatorPanel::modColour(int index) noexcept
{
    static const juce::Colour colours[] = {
        juce::Colour(0xFF1D9E75),  // A: teal
        juce::Colour(0xFFEF9F27),  // B: amber
        juce::Colour(0xFFD4537E),  // C: pink
        juce::Colour(0xFF378ADD),  // D: blue
        juce::Colour(0xFF7F77DD),  // E: purple
        juce::Colour(0xFFD85A30),  // F: coral
        juce::Colour(0xFF2BB5C5),  // G: cyan-teal
        juce::Colour(0xFF888780),  // H: grey
    };
    return colours[juce::jlimit(0, 7, index)];
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
}

void ModulatorPanel::setRhythm(Rhythm* r)
{
    rhythm = r;
    if (!r) return;

    for (int i = 0; i < kNumMods; ++i)
    {
        editors[i].setData(&r->controlSequences[i], &r->modulationMatrix, modColour(i), i);
        editors[i].onChange = [this] { if (onChange) onChange(); };
    }
    matrixPanel.setRhythm(r);
    matrixPanel.onChange = [this] { if (onChange) onChange(); };
}

void ModulatorPanel::resized()
{
    const int w = getWidth(), h = getHeight();
    tabBar.setBounds(0, 0, w, kTabH);
    const juce::Rectangle<int> content(0, kTabH, w, h - kTabH);
    for (auto& e : editors) e.setBounds(content);
    matrixPanel.setBounds(content);
}

void ModulatorPanel::paint(juce::Graphics& g)
{
    g.setColour(MuClidLookAndFeel::colour(MuClidLookAndFeel::panelBackground));
    g.fillAll();
}
