#include "RhythmCircle.h"

RhythmCircle::RhythmCircle()
{
    arcPulses.fill({});
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

void RhythmCircle::setPlayState(PluginProcessor::RhythmPlayState*  state,
                                 const juce::Atomic<float>*          beatFrac,
                                 const juce::Atomic<bool>*            playing,
                                 juce::Colour                         colour)
{
    playState     = state;
    beatFracAtom  = beatFrac;
    isPlayingAtom = playing;
    rhythmColour  = colour;
}

//==============================================================================
void RhythmCircle::triggerHitPulse(int step, int patLen)
{
    if (patLen <= 0) return;
    auto& p    = arcPulses[nextPulse % kMaxPulses];
    p.stepFrac = (float)step / (float)patLen;
    p.arcWidth = juce::MathConstants<float>::twoPi / (float)patLen;
    p.alpha    = 0.7f;
    p.expand   = 0.0f;
    p.active   = true;
    ++nextPulse;
    hubAlpha = 0.5f;
}

void RhythmCircle::timerCallback()
{
    bool dirty = false;

    // ── Read play state ──────────────────────────────────────────────────────
    if (playState && beatFracAtom && isPlayingAtom)
    {
        const bool playing = isPlayingAtom->get();

        if (playing)
        {
            const int   step   = playState->currentStep  .get();
            const int   patLen = playState->patternLength .get();
            const float frac   = beatFracAtom->get();

            if (patLen > 0)
                rotationAngle = ((float)step + frac) / (float)patLen
                                * juce::MathConstants<float>::twoPi;

            if (playState->hitFired.get())
            {
                playState->hitFired.set(false);
                triggerHitPulse(step, patLen);
            }

            snapFromAngle = rotationAngle;
            snapProgress  = 1.0f;
            wasPlaying    = true;
            dirty         = true;
        }
        else if (wasPlaying)
        {
            wasPlaying   = false;
            snapProgress = 0.0f;
        }
    }

    // ── Ease-out snap to 0 when stopped ─────────────────────────────────────
    if (!wasPlaying && snapProgress < 1.0f)
    {
        snapProgress = juce::jmin(1.0f, snapProgress + (1.0f / (0.15f * 30.0f)));
        const float t     = snapProgress;
        const float ease  = t * (2.0f - t);
        rotationAngle     = snapFromAngle * (1.0f - ease);
        dirty             = true;
    }

    // ── Advance arc pulses ───────────────────────────────────────────────────
    static constexpr float kPulseSteps = 0.15f * 30.0f; // 150ms at 30Hz
    for (auto& p : arcPulses)
    {
        if (!p.active) continue;
        p.expand = juce::jmin(1.0f, p.expand + (1.0f / kPulseSteps));
        const float t = p.expand;
        p.alpha = 0.7f * (1.0f - t * (2.0f - t));
        if (p.alpha <= 0.0f) p.active = false;
        dirty = true;
    }

    // ── Hub pulse decay (300ms at 30Hz) ──────────────────────────────────────
    if (hubAlpha > 0.0f)
    {
        hubAlpha = juce::jmax(0.0f, hubAlpha - (0.5f / (0.3f * 30.0f)));
        dirty = true;
    }

    if (dirty) repaint();
}

//==============================================================================
juce::Colour RhythmCircle::stepColour(StepType t, juce::Colour hitClr, bool isCurrent)
{
    using Id = MuClidLookAndFeel::ColourIds;
    juce::Colour base;
    switch (t)
    {
        case StepType::Hit:       base = hitClr; break;
        case StepType::PrePad:    base = MuClidLookAndFeel::colour(Id::ringPrePad)   .withAlpha(0.5f); break;
        case StepType::PostPad:   base = MuClidLookAndFeel::colour(Id::ringPostPad)  .withAlpha(0.5f); break;
        case StepType::InsertPad: base = MuClidLookAndFeel::colour(Id::ringInsertPad).withAlpha(0.5f); break;
        default:                  base = hitClr.withAlpha(0.18f); break;
    }
    if (isCurrent) base = base.brighter(0.5f);
    return base;
}

void RhythmCircle::drawRing(juce::Graphics& g,
                              const std::vector<StepType>& pattern,
                              float cx, float cy,
                              float outerR, float innerR,
                              juce::Colour hitClr,
                              int currentStep,
                              float rotOff,
                              bool dashed) const
{
    const int N = (int)pattern.size();
    if (N == 0 || outerR <= innerR || innerR < 0.0f) return;

    const float twoPi   = juce::MathConstants<float>::twoPi;
    const float startOff = -juce::MathConstants<float>::halfPi - rotOff;
    const float stepAng = twoPi / (float)N;
    const float gapAng  = juce::jmin(0.05f, stepAng * 0.15f);
    const float arcAng  = stepAng - gapAng;

    for (int i = 0; i < N; i++)
    {
        const float a0   = startOff + (float)i * stepAng;
        const float a1   = a0 + arcAng;
        const bool isCur = (i == currentStep);
        g.setColour(stepColour(pattern[i], hitClr, isCur));

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

    const int   curStep = playState ? playState->currentStep.get() : 0;
    const float rotOff  = rotationAngle;

    // ── Ring A — purple ──────────────────────────────────────────────────────
    const float aOuter = maxR;
    const float aInner = aOuter - ringW;
    if (!patternA.empty())
    {
        drawRing(g, patternA, cx, cy, aOuter, aInner,
                 MuClidLookAndFeel::colour(Id::ringEuclidA), curStep, rotOff);
    }
    else
    {
        g.setColour(MuClidLookAndFeel::colour(Id::ringEuclidA).withAlpha(0.12f));
        juce::Path ring;
        ring.addCentredArc(cx, cy, aOuter, aOuter, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
        ring.addCentredArc(cx, cy, aInner, aInner, 0.0f, juce::MathConstants<float>::twoPi, 0.0f, false);
        ring.closeSubPath();
        g.fillPath(ring);
    }

    // ── Ring B — coral ───────────────────────────────────────────────────────
    const float bOuter = aInner - ringGap;
    const float bInner = bOuter - ringW;
    float innerLimit   = (patternB.empty() ? aInner : bInner) - ringGap;

    if (bInner > 0.0f)
    {
        if (!patternB.empty())
        {
            drawRing(g, patternB, cx, cy, bOuter, bInner,
                     MuClidLookAndFeel::colour(Id::ringEuclidB), curStep, rotOff);
        }
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

    // ── Ring C — amber dashed ────────────────────────────────────────────────
    if (!patternC.empty())
    {
        const float cOuter = bInner - ringGap;
        const float cInner = cOuter - ringW;
        if (cInner > 0.0f)
        {
            drawRing(g, patternC, cx, cy, cOuter, cInner,
                     MuClidLookAndFeel::colour(Id::ringEuclidC), -1, rotOff, true);
            innerLimit = cInner - ringGap;
        }
    }

    // ── Expanding arc pulses ─────────────────────────────────────────────────
    for (const auto& p : arcPulses)
    {
        if (!p.active || p.alpha <= 0.0f) continue;

        const float expandPx   = p.expand * 14.0f;
        const float pulseOuter = aOuter + expandPx;

        const float twoPi  = juce::MathConstants<float>::twoPi;
        const float startA = -juce::MathConstants<float>::halfPi - rotOff
                             + p.stepFrac * twoPi - p.arcWidth * 0.5f;
        const float endA   = startA + p.arcWidth;

        g.setColour(rhythmColour.withAlpha(p.alpha));
        juce::Path pulse;
        pulse.addCentredArc(cx, cy, pulseOuter, pulseOuter, 0.0f, startA, endA, true);
        pulse.addCentredArc(cx, cy, aOuter,     aOuter,     0.0f, endA, startA, false);
        pulse.closeSubPath();
        g.fillPath(pulse);
    }

    // ── Centre fill (hub pulse under bg) ─────────────────────────────────────
    if (innerLimit > 4.0f)
    {
        if (hubAlpha > 0.0f)
        {
            g.setColour(rhythmColour.withAlpha(hubAlpha));
            g.fillEllipse(cx - innerLimit, cy - innerLimit,
                          innerLimit * 2.0f, innerLimit * 2.0f);
        }
        g.setColour(MuClidLookAndFeel::colour(Id::panelBackground));
        g.fillEllipse(cx - innerLimit, cy - innerLimit,
                      innerLimit * 2.0f, innerLimit * 2.0f);
    }
}
