#include "GrooveGrid.h"
#include "Sequencer/GrooveSequencer.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

GrooveGrid::GrooveGrid(ProcessorBase& processor, StepPattern& patternToEdit)
    : proc(processor), pattern(patternToEdit)
{
    auto setupKnob = [this](juce::Slider& s, juce::Label& lab, const juce::String& text)
    {
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(s);
        lab.setText(text, juce::dontSendNotification);
        lab.setJustificationType(juce::Justification::centred);
        lab.setFont(juce::Font(juce::FontOptions(12.0f)));
        addAndMakeVisible(lab);
    };
    setupKnob(swingSlider,  swingLabel,  "Swing");
    setupKnob(accentSlider, accentLabel, "Accent");

    swingAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "seq_swing",  swingSlider);
    accentAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "seq_accent", accentSlider);

    startTimerHz(30);   // playhead
}

juce::Colour GrooveGrid::trackColour(int t) const
{
    return MuLookAndFeel::channelPalette[
        (size_t) juce::jlimit(0, MuLookAndFeel::kChannelPaletteSize - 1, proc.getChannelColourIndex(t))];
}

juce::Rectangle<int> GrooveGrid::gridArea() const
{
    return getLocalBounds().withTrimmedTop(kHeaderH).reduced(8);
}

void GrooveGrid::resized()
{
    auto header = getLocalBounds().removeFromTop(kHeaderH).reduced(8, 4);
    const int knobW = 54;
    auto place = [&](juce::Slider& s, juce::Label& lab)
    {
        auto col = header.removeFromLeft(knobW);
        lab.setBounds(col.removeFromBottom(14));
        s.setBounds(col);
        header.removeFromLeft(8);
    };
    place(swingSlider, swingLabel);
    place(accentSlider, accentLabel);
}

void GrooveGrid::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    auto area = gridArea();
    const int rows  = StepPattern::kNumTracks;
    const int steps = StepPattern::kNumSteps;
    const int cellsX = area.getX() + kLabelW;
    const int cellsW = area.getWidth() - kLabelW;
    const int rowH   = (area.getHeight() - (rows - 1) * kRowGap) / rows;
    const float cellW = cellsW / (float) steps;

    for (int t = 0; t < rows; ++t)
    {
        const int rowY = area.getY() + t * (rowH + kRowGap);
        const auto col = trackColour(t);

        // Row label (instrument name); selected row gets a stronger tint.
        auto labelR = juce::Rectangle<int>(area.getX(), rowY, kLabelW - 6, rowH);
        g.setColour(col.withAlpha(t == selectedTrack ? 0.30f : 0.14f));
        g.fillRoundedRectangle(labelR.toFloat(), 4.0f);
        g.setColour(col);
        g.setFont(juce::Font(juce::FontOptions((float) juce::jmin(15, rowH - 6), juce::Font::bold)));
        g.drawText(proc.getChannelName(t), labelR.reduced(4, 0), juce::Justification::centredLeft, false);

        for (int s = 0; s < steps; ++s)
        {
            juce::Rectangle<float> cell((float) cellsX + s * cellW + 1.5f, (float) rowY + 1.5f,
                                        cellW - 3.0f, (float) rowH - 3.0f);

            const bool isBeat = (s % 4) == 0;        // beat boundary — brighter base
            const bool isPlay = (s == playheadStep);

            // Cell background — beat groups slightly lighter; playhead column highlighted.
            g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(isBeat ? 0.55f : 0.30f));
            if (isPlay) g.setColour(col.withAlpha(0.22f));
            g.fillRoundedRectangle(cell, 3.0f);

            if (pattern.isOn(t, s))
            {
                g.setColour(col.withAlpha(0.95f));
                g.fillRoundedRectangle(cell, 3.0f);
                if (pattern.isAccent(t, s))   // accent = brighter inner pip
                {
                    g.setColour(juce::Colours::white.withAlpha(0.85f));
                    g.fillRoundedRectangle(cell.reduced(cell.getWidth() * 0.32f, cell.getHeight() * 0.32f), 2.0f);
                }
            }
        }
    }
}

bool GrooveGrid::cellAt(juce::Point<int> p, int& track, int& step) const
{
    auto area = gridArea();
    const int rows  = StepPattern::kNumTracks;
    const int steps = StepPattern::kNumSteps;
    const int cellsX = area.getX() + kLabelW;
    const int cellsW = area.getWidth() - kLabelW;
    const int rowH   = (area.getHeight() - (rows - 1) * kRowGap) / rows;
    const float cellW = cellsW / (float) steps;

    if (p.x < cellsX || p.x >= cellsX + cellsW) return false;
    step = juce::jlimit(0, steps - 1, (int) ((p.x - cellsX) / cellW));
    for (int t = 0; t < rows; ++t)
    {
        const int rowY = area.getY() + t * (rowH + kRowGap);
        if (p.y >= rowY && p.y < rowY + rowH) { track = t; return true; }
    }
    return false;
}

void GrooveGrid::mouseDown(const juce::MouseEvent& e)
{
    int t, s;
    if (! cellAt(e.getPosition(), t, s)) return;

    if (e.mods.isRightButtonDown())
        pattern.setAccent(t, s, ! pattern.isAccent(t, s));   // right-click → accent
    else
        pattern.toggle(t, s);                                 // left-click → on/off

    repaint();
}

void GrooveGrid::timerCallback()
{
    const int step = proc.isInternalPlaying()
                        ? GrooveSequencer::currentStep(proc.getInternalBeatPos()) : -1;
    if (step != playheadStep) { playheadStep = step; repaint(); }
}

} // namespace mu_on
