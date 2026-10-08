#include "GrooveGrid.h"
#include "Sequencer/GrooveSequencer.h"
#include "UI/Components/MuLookAndFeel.h"

namespace mu_on
{

GrooveGrid::GrooveGrid(ProcessorBase& processor, StepPattern& patternToEdit)
    : proc(processor), pattern(patternToEdit)
{
    addAndMakeVisible(swingKnob);
    addAndMakeVisible(accentKnob);
    swingAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "seq_swing",  swingKnob.getSlider());
    accentAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "seq_accent", accentKnob.getSlider());

    startTimerHz(mu_ui::kUiRefreshHz);   // playhead
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

// The cell row for the selected lane: flat, gridArea minus the lane-title band; metal, inside
// the steps box, clear of its corner screws.
juce::Rectangle<int> GrooveGrid::rowArea() const
{
    using LF = MuLookAndFeel;
    if (LF::isMetal(*this))
        return getLocalBounds().withTrimmedLeft(mu_ui::s(kGrooveBoxW + LF::kVoiceDivW))
                               .reduced(mu_ui::s(LF::kSubPanelScrewClear), mu_ui::s(LF::kSpaceS));
    return gridArea().withTrimmedTop(mu_ui::s(kTitleH));
}

void GrooveGrid::resized()
{
    // Metal: Swing / Accent centred in the Groove box; flat: a strip along the top.
    auto header = MuLookAndFeel::isMetal(*this)
                ? getLocalBounds().removeFromLeft(mu_ui::s(kGrooveBoxW)).reduced(mu_ui::s(MuLookAndFeel::kSubPanelScrewClear), 0)
                                  .withSizeKeepingCentre(mu_ui::s(kGrooveBoxW - 2 * MuLookAndFeel::kSubPanelScrewClear), mu_ui::s(kHeaderH))
                : getLocalBounds().removeFromTop(mu_ui::s(kHeaderH)).reduced(mu_ui::s(8), mu_ui::s(4));
    const int knobW = mu_ui::s(MuLookAndFeel::kKnobSize2W);
    for (auto* k : { &swingKnob, &accentKnob })
    {
        k->setBounds(header.removeFromLeft(knobW));
        header.removeFromLeft(mu_ui::s(MuLookAndFeel::kSpaceS));
    }
}

void GrooveGrid::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    const int  t     = selectedTrack;          // single-lane editor: only the selected lane
    const int  steps = StepPattern::kNumSteps;
    const auto col   = trackColour(t);

    if (MuLookAndFeel::isMetal(*this))
    {
        paintMetal(g, t, steps, col);
        return;
    }
    g.fillAll(MuLookAndFeel::colour(Id::panelBackground));

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
            g.drawText(mu_ui::cachedIntLabel(s + 1), cell.toNearestInt(), juce::Justification::topLeft, false);
        }
    }
}

// Metal style (the host draws the boxes and their name plates): each step a lamp behind a
// dark lens — on steps lit, accents brighter at the centre, beat starts marked by a hint of
// colour; empty steps stay dark even under the playhead (family lamp rule).
void GrooveGrid::paintMetal(juce::Graphics& g, int t, int steps, juce::Colour col)
{
    const auto& L = MuLookAndFeel::lighting();

    auto row = rowArea();
    const float cellW = row.getWidth() / (float) steps;
    const float rowH  = (float) row.getHeight();
    for (int s = 0; s < steps; ++s)
    {
        const juce::Rectangle<float> cell((float) row.getX() + s * cellW + 1.5f, (float) row.getY() + 1.5f,
                                          cellW - 3.0f, rowH - 3.0f);
        const bool on     = pattern.isOn(t, s);
        const bool isBeat = (s % 4) == 0;
        float lit = on ? L.lampOn : (isBeat ? L.lampBeat : L.lampOff);
        if (on && s == playheadStep) lit = juce::jmin(1.0f, lit + L.lampPlayhead);

        const auto lens = MuLookAndFeel::lampColour(col, lit);
        juce::Path shape;
        shape.addRoundedRectangle(cell, 3.0f);
        if (on)
        {
            MuLookAndFeel::drawLamp(g, shape, {}, lens, cell.getCentre(), juce::jmax(cell.getWidth(), cell.getHeight()) * 0.6f);
            if (pattern.isAccent(t, s))   // accent: a hotter centre
            {
                g.setColour(lens.brighter(L.lampHot * 2.0f));
                g.fillRoundedRectangle(cell.reduced(cell.getWidth() * 0.30f, cell.getHeight() * 0.30f), 2.0f);
            }
        }
        else
        {
            g.setColour(lens);
            g.fillPath(shape);
        }
        MuLookAndFeel::drawRecessedScreen(g, cell);

        if (isBeat)   // step number on the beat-group starts (1/5/9/13)
        {
            g.setFont(juce::Font(juce::FontOptions(mu_ui::sf(9.0f))));
            MuLookAndFeel::drawEngravedText(g, mu_ui::cachedIntLabel(s + 1), cell.reduced(2.0f).toNearestInt(),
                                            juce::Justification::topLeft, MuLookAndFeel::colour(MuLookAndFeel::labelText), false);
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
    if (step == playheadStep) return;

    // Repaint only the cells the playhead left and entered, not the whole grid.
    const auto  row   = rowArea();
    const float cellW = row.getWidth() / (float) StepPattern::kNumSteps;
    for (int s : { playheadStep, step })
        if (s >= 0)
            repaint(juce::Rectangle<float>((float) row.getX() + s * cellW, (float) row.getY(), cellW, (float) row.getHeight())
                        .getSmallestIntegerContainer());
    playheadStep = step;
}

} // namespace mu_on
