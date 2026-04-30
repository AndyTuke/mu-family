#include "RhythmCircle.h"

RhythmCircle::RhythmCircle()
{
    startTimerHz(30);
}

RhythmCircle::~RhythmCircle()
{
    stopTimer();
}

void RhythmCircle::setPatterns(const std::vector<StepType>& patA,
                                const std::vector<StepType>& patB,
                                const std::vector<StepType>& patC)
{
    patternA = patA;
    patternB = patB;
    patternC = patC;
    repaint();
}

void RhythmCircle::setCurrentSteps(int stepA, int stepB)
{
    currentStepA = stepA;
    currentStepB = stepB;
    repaint();
}

void RhythmCircle::pulseA()
{
    pulseAlphaA = 1.0f;
    repaint();
}

void RhythmCircle::pulseB()
{
    pulseAlphaB = 1.0f;
    repaint();
}

void RhythmCircle::timerCallback()
{
    bool dirty = false;
    if (pulseAlphaA > 0.0f) { pulseAlphaA = juce::jmax(0.0f, pulseAlphaA - 0.06f); dirty = true; }
    if (pulseAlphaB > 0.0f) { pulseAlphaB = juce::jmax(0.0f, pulseAlphaB - 0.06f); dirty = true; }
    if (dirty) repaint();
}

juce::Colour RhythmCircle::stepColour(StepType t, juce::Colour hitColour,
                                       bool isCurrent, float pulseAlpha)
{
    using Id = MuClidLookAndFeel::ColourIds;
    juce::Colour base;
    switch (t)
    {
        case StepType::Hit:       base = hitColour; break;
        case StepType::PrePad:    base = MuClidLookAndFeel::colour(Id::ringPrePad).withAlpha(0.5f); break;
        case StepType::PostPad:   base = MuClidLookAndFeel::colour(Id::ringPostPad).withAlpha(0.5f); break;
        case StepType::InsertPad: base = MuClidLookAndFeel::colour(Id::ringInsertPad).withAlpha(0.5f); break;
        default:                  base = hitColour.withAlpha(0.18f); break;
    }
    if (isCurrent) base = base.brighter(0.5f);
    if (pulseAlpha > 0.0f && t == StepType::Hit)
        base = base.interpolatedWith(juce::Colours::white, pulseAlpha * 0.35f);
    return base;
}

void RhythmCircle::drawRing(juce::Graphics& g,
                              const std::vector<StepType>& pattern,
                              float cx, float cy,
                              float outerR, float innerR,
                              juce::Colour hitColour,
                              int currentStep,
                              float pulseAlpha,
                              bool dashed)
{
    const int N = (int)pattern.size();
    if (N == 0 || outerR <= innerR || innerR < 0.0f) return;

    const float twoPi    = juce::MathConstants<float>::twoPi;
    const float startOff = -juce::MathConstants<float>::halfPi;
    const float stepAng  = twoPi / (float)N;
    const float gapAng   = juce::jmin(0.05f, stepAng * 0.15f);
    const float arcAng   = stepAng - gapAng;

    for (int i = 0; i < N; i++)
    {
        const float a0 = startOff + (float)i * stepAng;
        const float a1 = a0 + arcAng;
        const bool isCurrent = (i == currentStep);

        juce::Colour c = stepColour(pattern[i], hitColour, isCurrent, pulseAlpha);

        g.setColour(c);

        if (dashed)
        {
            const float midR  = (outerR + innerR) * 0.5f;
            const float halfW = (outerR - innerR) * 0.5f;
            juce::Path arc;
            arc.addCentredArc(cx, cy, midR, midR, 0.0f, a0, a1, true);
            float dashes[] = { 3.0f, 2.0f };
            juce::Path dashedPath;
            juce::PathStrokeType(halfW * 2.0f).createDashedStroke(dashedPath, arc, dashes, 2);
            g.fillPath(dashedPath);
        }
        else
        {
            juce::Path seg;
            seg.addCentredArc(cx, cy, outerR, outerR, 0.0f, a0, a1, true);
            seg.addCentredArc(cx, cy, innerR, innerR, 0.0f, a1, a0, false);
            seg.closeSubPath();
            g.fillPath(seg);
        }
    }
}

void RhythmCircle::paint(juce::Graphics& g)
{
    using Id = MuClidLookAndFeel::ColourIds;

    const float cx   = getWidth()  * 0.5f;
    const float cy   = getHeight() * 0.5f;
    const float maxR = juce::jmin(cx, cy) - 3.0f;

    if (maxR < 8.0f) return;

    const float ringW   = juce::jmax(4.0f, maxR * 0.16f);
    const float ringGap = juce::jmax(2.0f, maxR * 0.05f);

    // Ring A — outermost, purple
    const float aOuter = maxR;
    const float aInner = aOuter - ringW;
    if (!patternA.empty())
        drawRing(g, patternA, cx, cy, aOuter, aInner,
                 MuClidLookAndFeel::colour(Id::ringEuclidA),
                 currentStepA, pulseAlphaA);
    else
    {
        g.setColour(MuClidLookAndFeel::colour(Id::ringEuclidA).withAlpha(0.12f));
        juce::Path ring;
        ring.addCentredArc(cx, cy, aOuter, aOuter, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        ring.addCentredArc(cx, cy, aInner, aInner, 0.0f, juce::MathConstants<float>::twoPi, 0.0f, false);
        ring.closeSubPath();
        g.fillPath(ring);
    }

    // Ring B — coral
    const float bOuter = aInner - ringGap;
    const float bInner = bOuter - ringW;
    float innerLimit = (patternB.empty() ? aInner : bInner) - ringGap;

    if (bInner > 0.0f)
    {
        if (!patternB.empty())
            drawRing(g, patternB, cx, cy, bOuter, bInner,
                     MuClidLookAndFeel::colour(Id::ringEuclidB),
                     currentStepB, pulseAlphaB);
        else
        {
            g.setColour(MuClidLookAndFeel::colour(Id::ringEuclidB).withAlpha(0.12f));
            juce::Path ring;
            ring.addCentredArc(cx, cy, bOuter, bOuter, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
            ring.addCentredArc(cx, cy, bInner, bInner, 0.0f, juce::MathConstants<float>::twoPi, 0.0f, false);
            ring.closeSubPath();
            g.fillPath(ring);
        }
    }

    // Ring C — amber dashed (accent)
    if (!patternC.empty())
    {
        const float cOuter = bInner - ringGap;
        const float cInner = cOuter - ringW;
        if (cInner > 0.0f)
        {
            drawRing(g, patternC, cx, cy, cOuter, cInner,
                     MuClidLookAndFeel::colour(Id::ringEuclidC),
                     -1, 0.0f);
            innerLimit = cInner - ringGap;
        }
    }

    // Centre fill — panel bg colour to punch a hole in any underlapping arcs
    if (innerLimit > 4.0f)
    {
        g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
        g.fillEllipse(cx - innerLimit, cy - innerLimit, innerLimit * 2.0f, innerLimit * 2.0f);
    }
}
