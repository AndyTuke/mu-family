#include "StepEditor.h"
#include <cmath>

StepEditor::StepEditor()
{
    steps.assign(8, 0.0f);
}

void StepEditor::setSteps(const std::vector<float>& values)
{
    steps = values;
    repaint();
}

void StepEditor::setStepCount(int count)
{
    if (count < 1 || count > 4096) { jassertfalse; return; }
    steps.resize((size_t)count, 0.0f);
    repaint();
}

void StepEditor::setBarColour(juce::Colour c)
{
    barColour = c;
    repaint();
}

void StepEditor::setPlayheadPhase(float phase)
{
    if (std::abs(phase - playheadPhase) < 1.0f / juce::jmax(1, getWidth())) return;
    playheadPhase = phase;
    repaint();
}

int StepEditor::hitStepIndex(int x) const
{
    const int n = (int)steps.size();
    if (n == 0 || getWidth() <= 0) return -1;
    const float px = (float)x / (float)getWidth();   // 0..1 across the loop
    // Tile by step width (so a click in the narrow final cell still resolves to it) or
    // fall back to equal cells when no fraction is set.
    const int idx = (stepFraction > 0.0f && stepFraction < 1.0f)
                  ? (int)(px / stepFraction)
                  : (int)(px * (float)n);
    return juce::jlimit(0, n - 1, idx);
}

float StepEditor::yToValue(int y) const
{
    const float norm = 1.0f - (float)y / (float)getHeight(); // 0=bottom, 1=top
    float v;
    if (unipolar)
        v = juce::jlimit(0.0f, 100.0f, norm * 100.0f);
    else
        v = juce::jlimit(-100.0f, 100.0f, (norm - 0.5f) * 200.0f);

    if (quantizeLevels > 1)
    {
        const float lo   = unipolar ? 0.0f : -100.0f;
        const float hi   = 100.0f;
        const float step = (hi - lo) / (float)(quantizeLevels - 1);
        v = lo + step * std::round((v - lo) / step);
        v = juce::jlimit(lo, hi, v);
    }
    return v;
}

void StepEditor::applyAt(int x, int y)
{
    int idx = hitStepIndex(x);
    if (idx < 0) return;
    float v = yToValue(y);
    steps[(size_t)idx] = v;
    repaint();
    if (onStepChanged) onStepChanged(idx, v);
}

void StepEditor::paint(juce::Graphics& g)
{
    using Id = MuLookAndFeel::ColourIds;

    g.setColour(MuLookAndFeel::colour(Id::stepEditorBackground));
    g.fillAll();

    const int n = (int)steps.size();
    if (n == 0) return;

    const float w = (float)getWidth();
    const float h = (float)getHeight();

    // Cell boundaries: tile by the step fraction with a narrower partial final cell when set,
    // else equal 1/n cells. Matches the audio (evaluateStepped) + the smooth editor's grid.
    const bool tiled = (stepFraction > 0.0f && stepFraction < 1.0f);
    auto cellL = [&](int i){ return (tiled ? (float)i * stepFraction : (float)i / (float)n) * w; };
    auto cellR = [&](int i){ return (tiled ? juce::jmin(1.0f, (float)(i + 1) * stepFraction)
                                           : (float)(i + 1) / (float)n) * w; };
    auto barW  = [&](int i){ return juce::jmax(1.0f, cellR(i) - cellL(i) - 2.0f); };

    // Grid dividers at each internal cell boundary
    g.setColour(MuLookAndFeel::colour(Id::stepEditorGridLine));
    for (int i = 1; i < n; ++i)
        g.drawVerticalLine((int)cellL(i), 0, h);

    // Bars
    if (unipolar)
    {
        for (int i = 0; i < n; ++i)
        {
            const float v    = juce::jmax(0.0f, steps[(size_t)i]); // clamp to 0..100
            const float barH = (v / 100.0f) * h;
            g.setColour(barColour.withAlpha(0.85f));
            g.fillRect(cellL(i) + 1.0f, h - barH, barW(i), barH);
        }
    }
    else
    {
        const float centY = h * 0.5f;
        for (int i = 0; i < n; ++i)
        {
            const float v      = steps[(size_t)i]; // -100..+100
            const float norm   = v / 200.0f;       // -0.5..+0.5
            const float barH   = std::abs(norm) * h;
            const float barTop = (norm >= 0.0f) ? centY - barH : centY;
            g.setColour(barColour.withAlpha(0.85f));
            g.fillRect(cellL(i) + 1.0f, barTop, barW(i), barH);
        }
        // Centre zero line (bipolar only)
        g.setColour(MuLookAndFeel::colour(Id::stepEditorZeroLine));
        g.drawHorizontalLine((int)(h * 0.5f), 0.0f, (float)getWidth());
    }

    // Bottom zero line (unipolar) or centre zero line (already drawn for bipolar above)
    if (unipolar)
    {
        g.setColour(MuLookAndFeel::colour(Id::stepEditorZeroLine));
        g.drawHorizontalLine((int)(h - 1), 0.0f, (float)getWidth());
    }

    // Playhead
    const float phX = playheadPhase * (float)getWidth();
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.drawVerticalLine((int)phX, 0.0f, h);
}

void StepEditor::mouseDown(const juce::MouseEvent& e)
{
    applyAt(e.x, e.y);
}

void StepEditor::mouseDrag(const juce::MouseEvent& e)
{
    applyAt(e.x, e.y);
}
