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
        lab.setFont(juce::Font(juce::FontOptions(mu_ui::sf(12.0f))));
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
    return getLocalBounds().withTrimmedTop(mu_ui::s(kHeaderH)).reduced(mu_ui::s(8));
}

// The cell row for the selected lane (gridArea minus the lane-title band).
juce::Rectangle<int> GrooveGrid::rowArea() const
{
    return gridArea().withTrimmedTop(mu_ui::s(kTitleH));
}

void GrooveGrid::resized()
{
    auto header = getLocalBounds().removeFromTop(mu_ui::s(kHeaderH)).reduced(mu_ui::s(8), mu_ui::s(4));
    const int knobW = mu_ui::s(54);
    auto place = [&](juce::Slider& s, juce::Label& lab)
    {
        auto col = header.removeFromLeft(knobW);
        lab.setBounds(col.removeFromBottom(mu_ui::s(14)));
        s.setBounds(col);
        header.removeFromLeft(mu_ui::s(8));
    };
    place(swingSlider, swingLabel);
    place(accentSlider, accentLabel);
}

void GrooveGrid::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

    const int  t     = selectedTrack;          // single-lane editor: only the selected lane
    const int  steps = StepPattern::kNumSteps;
    const auto col   = trackColour(t);

    // Lane title above the step row.
    auto title = gridArea().removeFromTop(mu_ui::s(kTitleH));
    g.setColour(col);
    g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(15.0f), juce::Font::bold)));
    g.drawText(proc.getChannelName(t) + "  steps", title, juce::Justification::centredLeft, false);

    auto row = rowArea();
    const float cellW = row.getWidth() / (float) steps;
    const float rowH  = (float) row.getHeight();

    for (int s = 0; s < steps; ++s)
    {
        juce::Rectangle<float> cell((float) row.getX() + s * cellW + 1.5f, (float) row.getY() + 1.5f,
                                    cellW - 3.0f, rowH - 3.0f);

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
                g.fillRoundedRectangle(cell.reduced(cell.getWidth() * 0.30f, cell.getHeight() * 0.30f), 2.0f);
            }
        }

        // Step number under the beat-group starts (1/5/9/13).
        if (isBeat)
        {
            g.setColour(MuLookAndFeel::colour(Id::segmentInactiveBorder).withAlpha(0.7f));
            g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(9.0f))));
            g.drawText(juce::String(s + 1), cell.toNearestInt(), juce::Justification::topLeft, false);
        }
    }
}

bool GrooveGrid::cellAt(juce::Point<int> p, int& track, int& step) const
{
    auto row = rowArea();
    const int steps = StepPattern::kNumSteps;
    const float cellW = row.getWidth() / (float) steps;

    if (! row.contains(p)) return false;
    step  = juce::jlimit(0, steps - 1, (int) ((p.x - row.getX()) / cellW));
    track = selectedTrack;
    return true;
}

void GrooveGrid::mouseDown(const juce::MouseEvent& e)
{
    int t, s;
    if (! cellAt(e.getPosition(), t, s)) return;

    // Right-click toggles accent, but only on an ON step (accent is invisible/inert
    // otherwise); on an OFF step it turns the step on so the gesture is never a no-op.
    if (e.mods.isRightButtonDown())
    {
        if (pattern.isOn(t, s)) pattern.setAccent(t, s, ! pattern.isAccent(t, s));
        else                    { pattern.setOn(t, s, true); pattern.setAccent(t, s, true); }
    }
    else
    {
        pattern.toggle(t, s);                                 // left-click → on/off
    }

    repaint();
}

void GrooveGrid::timerCallback()
{
    const int step = proc.isInternalPlaying()
                        ? GrooveSequencer::currentStep(proc.getInternalBeatPos()) : -1;
    if (step != playheadStep) { playheadStep = step; repaint(); }
}

} // namespace mu_on
