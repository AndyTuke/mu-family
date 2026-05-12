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
void RhythmCircle::triggerHitPulse(int combinedStep, int stepsA)
{
    if (stepsA <= 0) return;
    const int ringAStep = combinedStep % stepsA;
    auto& p    = arcPulses[nextPulse % kMaxPulses];
    p.stepFrac = (float)ringAStep / (float)stepsA;
    p.arcWidth = juce::MathConstants<float>::twoPi / (float)stepsA;
    // #252: brighter flash on hit — was 0.7 alpha, now 1.0 so the playhead
    // pulse is unmistakable.
    p.alpha    = 1.0f;
    p.expand   = 0.0f;
    p.active   = true;
    ++nextPulse;
    hubAlpha = 0.8f;   // #252: also brighter hub flash (was 0.5)
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
            const int   step  = playState->currentStep.get();
            const float frac  = beatFracAtom->get();
            const int   sA    = juce::jmax(1, playState->stepsA.get());
            const int   sB    = juce::jmax(1, playState->stepsB.get());
            const int   sC    = juce::jmax(1, playState->stepsC.get());
            const float twoPi = juce::MathConstants<float>::twoPi;

            // Each ring rotates at its own speed: one full turn per sX steps.
            rotAngleA = ((float)(step % sA) + frac) / (float)sA * twoPi;
            rotAngleB = ((float)(step % sB) + frac) / (float)sB * twoPi;
            rotAngleC = ((float)(step % sC) + frac) / (float)sC * twoPi;

            // Issue #43: edge-detect via monotonic counter so multiple readers
            // (this circle + sidebar mini-circles + sidebar pulse) can all observe
            // the same hit without racing each other on a shared one-shot flag.
            const int currentHitCount = playState->hitCount.get();
            if (currentHitCount != lastHitCount)
            {
                lastHitCount = currentHitCount;
                triggerHitPulse(step, sA);
            }

            snapFromA    = rotAngleA;
            snapFromB    = rotAngleB;
            snapFromC    = rotAngleC;
            snapProgress = 1.0f;
            wasPlaying   = true;
            dirty        = true;
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
        const float ease = snapProgress * (2.0f - snapProgress);
        rotAngleA = snapFromA * (1.0f - ease);
        rotAngleB = snapFromB * (1.0f - ease);
        rotAngleC = snapFromC * (1.0f - ease);
        dirty     = true;
    }

    // ── Advance arc pulses ───────────────────────────────────────────────────
    static constexpr float kPulseSteps = 0.15f * 30.0f; // 150ms at 30Hz
    for (auto& p : arcPulses)
    {
        if (!p.active) continue;
        p.expand = juce::jmin(1.0f, p.expand + (1.0f / kPulseSteps));
        const float t = p.expand;
        p.alpha = 1.0f * (1.0f - t * (2.0f - t));   // #252: max alpha 1.0 (was 0.7)
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
                              float rotOff) const
{
    const int N = (int)pattern.size();
    if (N == 0 || outerR <= innerR || innerR < 0.0f) return;

    const float twoPi    = juce::MathConstants<float>::twoPi;
    // #252: JUCE's addCentredArc measures angles from 12 o'clock (top) and
    // increases clockwise (radians: 0 = top, π/2 = right, π = bottom, -π/2 =
    // left). Previously this used `-halfPi - rotOff`, which intended "start at
    // 12 o'clock" using standard math conventions but in JUCE's arc convention
    // placed step 0 at 9 o'clock — so hits visually fired at 9 o'clock instead
    // of 12. Drop the −halfPi offset so step 0 sits at the top when rotOff=0.
    const float startOff = -rotOff;
    const float stepAng  = twoPi / (float)N;
    const float gapAng   = juce::jmin(0.05f, stepAng * 0.15f);
    const float arcAng   = stepAng - gapAng;

    for (int i = 0; i < N; i++)
    {
        const float a0   = startOff + (float)i * stepAng;
        const float a1   = a0 + arcAng;
        const bool isCur = (i == currentStep);
        g.setColour(stepColour(pattern[i], hitClr, isCur));

        juce::Path seg;
        seg.addCentredArc(cx, cy, outerR, outerR, 0.0f, a0, a1, true);
        seg.addCentredArc(cx, cy, innerR, innerR, 0.0f, a1, a0, false);
        seg.closeSubPath();
        g.fillPath(seg);
    }

    // Loop-point divider: radial line at the boundary between step N-1 and step 0.
    // #252: use the same addCentredArc convention as the segments above —
    // x = cx + r·sin(angle), y = cy − r·cos(angle) — instead of cos/sin
    // standard-math (which left the divider at 12 o'clock while segments
    // started at 9). Now divider and segments share the same anchor.
    using Id = MuClidLookAndFeel::ColourIds;
    const float loopSin = std::sin(startOff);
    const float loopCos = std::cos(startOff);
    g.setColour(MuClidLookAndFeel::colour(Id::panelBackground).brighter(0.6f).withAlpha(0.85f));
    g.drawLine(cx + innerR * loopSin, cy - innerR * loopCos,
               cx + outerR * loopSin, cy - outerR * loopCos, 1.5f);
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

    // Per-ring current step is derived from pattern size below to avoid a race
    // between the pattern update and the stepsA/stepsB atomics.
    const int combinedStep = playState ? playState->currentStep.get() : 0;

    // ── Ring A — purple ──────────────────────────────────────────────────────
    const float aOuter = maxR;
    const float aInner = aOuter - ringW;
    // Use pattern size for modulo — avoids a race between pattern update and stepsA atomic.
    const int stepA = patternA.empty() ? 0 : combinedStep % (int)patternA.size();
    if (!patternA.empty())
    {
        drawRing(g, patternA, cx, cy, aOuter, aInner,
                 MuClidLookAndFeel::colour(Id::ringEuclidA), stepA, rotAngleA);
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

    const int stepB = patternB.empty() ? 0 : combinedStep % (int)patternB.size();
    if (bInner > 0.0f)
    {
        if (!patternB.empty())
        {
            drawRing(g, patternB, cx, cy, bOuter, bInner,
                     MuClidLookAndFeel::colour(Id::ringEuclidB), stepB, rotAngleB);
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
                     MuClidLookAndFeel::colour(Id::ringEuclidC), -1, rotAngleC);
            innerLimit = cInner - ringGap;
        }
    }

    // ── Expanding arc pulses (expand outward from Ring A, rotate with Ring A) ─
    for (const auto& p : arcPulses)
    {
        if (!p.active || p.alpha <= 0.0f) continue;

        const float expandPx   = p.expand * 14.0f;
        const float pulseOuter = aOuter + expandPx;

        const float twoPi  = juce::MathConstants<float>::twoPi;
        // #252: drop the −halfPi offset so the pulse aligns with 12 o'clock —
        // mirrors the same fix in drawRing's startOff.
        const float startA = -rotAngleA + p.stepFrac * twoPi - p.arcWidth * 0.5f;
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
