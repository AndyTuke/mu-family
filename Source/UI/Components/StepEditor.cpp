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
    if (steps.empty()) return -1;
    const int barW = getWidth() / (int)steps.size();
    int idx = x / barW;
    return juce::jlimit(0, (int)steps.size() - 1, idx);
}

float StepEditor::yToValue(int y) const
{
    // Centre of component = 0, top = +100, bottom = -100
    const float norm = 1.0f - (float)y / (float)getHeight(); // 0..1, 0=bottom
    return juce::jlimit(-100.0f, 100.0f, (norm - 0.5f) * 200.0f);
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
    using Id = MuClidLookAndFeel::ColourIds;

    g.setColour(MuClidLookAndFeel::colour(Id::stepEditorBackground));
    g.fillAll();

    const int n = (int)steps.size();
    if (n == 0) return;

    const float barW  = (float)getWidth() / n;
    const float h     = (float)getHeight();
    const float centY = h * 0.5f;

    // Grid dividers
    g.setColour(MuClidLookAndFeel::colour(Id::stepEditorGridLine));
    for (int i = 1; i < n; ++i)
        g.drawVerticalLine((int)(i * barW), 0, h);

    // Bars
    for (int i = 0; i < n; ++i)
    {
        const float v      = steps[(size_t)i]; // -100..+100
        const float norm   = v / 200.0f;       // -0.5..+0.5
        const float barH   = std::abs(norm) * h;
        const float barTop = (norm >= 0.0f) ? centY - barH : centY;

        g.setColour(barColour.withAlpha(0.85f));
        g.fillRect(i * barW + 1.0f, barTop, barW - 2.0f, barH);
    }

    // Centre zero line
    g.setColour(MuClidLookAndFeel::colour(Id::stepEditorZeroLine));
    g.drawHorizontalLine((int)centY, 0.0f, (float)getWidth());

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
